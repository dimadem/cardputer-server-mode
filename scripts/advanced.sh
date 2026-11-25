#!/bin/bash
# Boot-time (reboot required) tweaks for Server Mode.
#   advanced.sh status                        print current values (no root needed)
#   advanced.sh apply key=value... [--reboot] keys: gpu=16|32|64|128, boot_server|camera_off|hdmi_off|cloudinit_off=0|1,
#                                             groups=a,b (groups the boot unit stops, used with boot_server=1)
#   advanced.sh reset [--reboot]              restore the original boot files and defaults
# The first apply saves config.txt/cmdline.txt as *.cz-orig; reset puts them back.
# Boot files are replaced through a synced temp file and a rename. The FAT boot partition gives
# no atomic rename, so this only makes a broken file after a power cut less likely.
set -eu
# shellcheck source=common.sh
. "$(dirname "$(readlink -f "$0")")/common.sh"

CFG=/boot/firmware/config.txt
CMD=/boot/firmware/cmdline.txt
ORIG_CFG=$CFG.cz-orig
ORIG_CMD=$CMD.cz-orig
CLOUD_OFF=/etc/cloud/cloud-init.disabled
BOOT_UNIT=/etc/systemd/system/cz-server-mode-boot.service
HDMI_ARG="video=HDMI-A-1:d"
SCRIPT_DIR=$(dirname "$(readlink -f "$0")")

# copy $1 to $2 through a synced temp file, so $2 is never written half-way
atomic_copy() {
  cp "$1" "$2.cz-tmp"
  sync "$2.cz-tmp"
  mv -f "$2.cz-tmp" "$2"
  sync
}

# gpu_mem lines that the CM0 reads: before any [section] or under [all]
gpu_value() {
  awk '/^[[:space:]]*\[/ { s = $0; gsub(/[][[:space:]]/, "", s); next }
       (s == "" || s == "all") && /^[[:space:]]*gpu_mem=/ { sub(/^[[:space:]]*gpu_mem=/, ""); v = $0 + 0 }
       END { if (v != "") print v }' "$1"
}

# set gpu_mem=$2 in file $1: replace the lines gpu_value reads, else add one under [all]
set_gpu() {
  awk -v val="$2" '
    /^[[:space:]]*\[/ { s = $0; gsub(/[][[:space:]]/, "", s); print; next }
    (s == "" || s == "all") && /^[[:space:]]*gpu_mem=/ { print "gpu_mem=" val; done = 1; next }
    { print }
    END { if (!done) { if (s != "" && s != "all") print "[all]"; print "gpu_mem=" val } }' "$1" > "$1.awk"
  mv -f "$1.awk" "$1"
}

boot_server_on() {
  [ -f "$BOOT_UNIT" ] && systemctl is-enabled --quiet cz-server-mode-boot.service \
    && [ "$(systemctl get-default)" = multi-user.target ]
}

status() {
  local gpu groups=""
  gpu=$(gpu_value "$CFG")
  [ -f "$BOOT_UNIT" ] && groups=$(sed -n 's/^ExecStart=.*--boot *//p' "$BOOT_UNIT" | tr ' ' ',')
  echo "gpu=${gpu:-64}" \
       "boot_server=$(boot_server_on && echo 1 || echo 0)" \
       "camera_off=$(grep -q '^#cz-off dtoverlay=imx219' "$CFG" && echo 1 || echo 0)" \
       "hdmi_off=$(grep -q "$HDMI_ARG" "$CMD" && echo 1 || echo 0)" \
       "cloudinit_off=$([ -e "$CLOUD_OFF" ] && echo 1 || echo 0)" \
       "modified=$([ -s "$ORIG_CFG" ] && echo 1 || echo 0)" \
       "boot_groups=${groups}"
}

set_boot_server() {  # $1 = 0|1, $2 = groups separated by spaces (not empty)
  if [ "$1" = 1 ]; then
    cat > "$BOOT_UNIT.cz-tmp" <<EOF
[Unit]
Description=CardputerZero Server Mode at boot
After=network.target cups.service accounts-daemon.service rpcbind.service
# the package was removed without "advanced.sh reset": do nothing
ConditionFileIsExecutable=$SCRIPT_DIR/server-mode.sh

[Service]
Type=oneshot
ExecStart=$SCRIPT_DIR/server-mode.sh --boot $2
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF
    mv -f "$BOOT_UNIT.cz-tmp" "$BOOT_UNIT"
    systemctl daemon-reload
    systemctl enable --quiet cz-server-mode-boot.service
    systemctl set-default multi-user.target >/dev/null
  else
    [ -f "$BOOT_UNIT" ] || return 0
    systemctl disable cz-server-mode-boot.service >/dev/null 2>&1 || true
    rm -f "$BOOT_UNIT"
    systemctl daemon-reload
    systemctl set-default graphical.target >/dev/null
  fi
}

reboot_soon() {
  # mpris-proxy (Bluetooth media proxy, user service) ignores SIGTERM and holds
  # the shutdown for its 90 s stop timeout. Give it 2 s for this shutdown only (/run is tmpfs).
  local d=/run/systemd/user/mpris-proxy.service.d
  mkdir -p "$d" && printf '[Service]\nTimeoutStopSec=2\n' > "$d/cz-fast-stop.conf"
  user_systemctl daemon-reload 2>/dev/null || true
  echo "rebooting in 3 s"
  systemd-run --quiet --on-active=3 systemctl reboot
}

bool_arg() {  # $1 = key, $2 = value: only 0 or 1
  case "$2" in 0|1) ;; *) echo "bad value for $1: $2 (0 or 1)" >&2; exit 2 ;; esac
}

apply() {
  need_root
  # 1. check all arguments before anything changes
  local do_reboot=0 gpu="" camera_off="" hdmi_off="" boot_server="" groups="" has_groups=0
  local cloudinit_off="" kv k v g
  for kv in "$@"; do
    [ "$kv" = --reboot ] && { do_reboot=1; continue; }
    case "$kv" in *=*) ;; *) echo "bad argument: $kv (key=value)" >&2; exit 2 ;; esac
    k=${kv%%=*}; v=${kv#*=}
    case "$k" in
      gpu) case "$v" in 16|32|64|128) gpu=$v ;; *) echo "bad gpu value: $v" >&2; exit 2 ;; esac ;;
      camera_off) bool_arg "$k" "$v"; camera_off=$v ;;
      hdmi_off) bool_arg "$k" "$v"; hdmi_off=$v ;;
      cloudinit_off) bool_arg "$k" "$v"; cloudinit_off=$v ;;
      boot_server) bool_arg "$k" "$v"; boot_server=$v ;;
      groups)
        has_groups=1; groups=${v//,/ }
        [ -n "$groups" ] || { echo "groups: give at least one group" >&2; exit 2; }
        for g in $groups; do group_units "$g" >/dev/null || { echo "unknown group: $g" >&2; exit 2; }; done ;;
      *) echo "unknown key: $k" >&2; exit 2 ;;
    esac
  done
  # groups alone (boot_server already on) rewrite the unit with the new selection
  if [ -z "$boot_server" ] && [ $has_groups = 1 ] && [ -f "$BOOT_UNIT" ]; then boot_server=1; fi
  if [ "$boot_server" = 1 ] && [ -z "$groups" ]; then
    echo "boot_server=1 needs groups=..." >&2; exit 2
  fi

  # 2. apply
  take_lock
  # a backup that is missing or empty (power cut during the first apply) is made again
  [ -s "$ORIG_CFG" ] || atomic_copy "$CFG" "$ORIG_CFG"
  [ -s "$ORIG_CMD" ] || atomic_copy "$CMD" "$ORIG_CMD"

  # edit working copies, then swap them in
  local new_cfg=$CFG.cz-new new_cmd=$CMD.cz-new
  cp "$CFG" "$new_cfg"
  cp "$CMD" "$new_cmd"
  [ -n "$gpu" ] && set_gpu "$new_cfg" "$gpu"
  case "$camera_off" in
    1) sed -i 's/^dtoverlay=imx219/#cz-off dtoverlay=imx219/' "$new_cfg" ;;
    0) sed -i 's/^#cz-off dtoverlay=imx219/dtoverlay=imx219/' "$new_cfg" ;;
  esac
  case "$hdmi_off" in
    1) grep -q "$HDMI_ARG" "$new_cmd" || sed -i "1 s/\$/ $HDMI_ARG/" "$new_cmd" ;;
    0) sed -i "s/ $HDMI_ARG//g" "$new_cmd" ;;
  esac
  cmp -s "$new_cfg" "$CFG" || atomic_copy "$new_cfg" "$CFG"
  cmp -s "$new_cmd" "$CMD" || atomic_copy "$new_cmd" "$CMD"
  rm -f "$new_cfg" "$new_cmd"

  case "$cloudinit_off" in
    1) mkdir -p "$(dirname "$CLOUD_OFF")" && touch "$CLOUD_OFF" ;;
    0) rm -f "$CLOUD_OFF" ;;
  esac
  [ -n "$boot_server" ] && set_boot_server "$boot_server" "$groups"
  sync
  echo "applied: $(status)"
  [ $do_reboot = 1 ] && reboot_soon
  return 0
}

reset() {
  need_root
  take_lock
  local f
  for f in "$ORIG_CFG" "$ORIG_CMD"; do
    # an empty backup is broken: keep the live file and the backup, and stop
    if [ -e "$f" ] && [ ! -s "$f" ]; then echo "backup is empty, not restored: $f" >&2; exit 1; fi
  done
  if [ -e "$ORIG_CFG" ]; then atomic_copy "$ORIG_CFG" "$CFG"; fi
  if [ -e "$ORIG_CMD" ]; then atomic_copy "$ORIG_CMD" "$CMD"; fi
  rm -f "$ORIG_CFG" "$ORIG_CMD"   # only after both restores are on disk
  set_boot_server 0 ""
  rm -f "$CLOUD_OFF"
  sync
  echo "reset: $(status)"
  [ "${1:-}" = --reboot ] && reboot_soon
  return 0
}

case "${1:-status}" in
  status) status ;;
  apply) shift; apply "$@" ;;
  reset) shift; reset "$@" ;;
  *) echo "usage: $0 status|apply key=value... [--reboot]|reset [--reboot]" >&2; exit 2 ;;
esac
