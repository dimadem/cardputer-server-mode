#!/bin/bash
# Boot-time (reboot required) tweaks for Server Mode.
#   advanced.sh status                        print current values (no root needed)
#   advanced.sh apply key=value... [--reboot] keys: gpu=16|32|64|128, boot_server|camera_off|hdmi_off|cloudinit_off=0|1,
#                                             groups=a,b (groups the boot unit stops, used with boot_server=1)
#   advanced.sh reset [--reboot]              restore the original boot files and defaults
# The first apply saves config.txt/cmdline.txt as *.cz-orig; reset puts them back.
# Boot files are always replaced atomically (temp file + sync + rename) so a power cut cannot truncate them.
set -eu
. "$(dirname "$(readlink -f "$0")")/common.sh"

CFG=/boot/firmware/config.txt
CMD=/boot/firmware/cmdline.txt
ORIG_CFG=$CFG.cz-orig
ORIG_CMD=$CMD.cz-orig
CLOUD_OFF=/etc/cloud/cloud-init.disabled
BOOT_UNIT=/etc/systemd/system/cz-server-mode-boot.service
HDMI_ARG="video=HDMI-A-1:d"

# copy $1 to $2 so that $2 is either the old or the complete new file
atomic_copy() {
  cp "$1" "$2.cz-tmp"
  sync "$2.cz-tmp"
  mv -f "$2.cz-tmp" "$2"
  sync
}

status() {
  local gpu groups=""
  gpu=$(sed -n 's/^gpu_mem=\([0-9]*\).*/\1/p' "$CFG" | tail -1)
  [ -f "$BOOT_UNIT" ] && groups=$(sed -n 's/^ExecStart=.*--boot *//p' "$BOOT_UNIT" | tr ' ' ',')
  echo "gpu=${gpu:-64}" \
       "boot_server=$([ -f "$BOOT_UNIT" ] && systemctl is-enabled --quiet cz-server-mode-boot.service && echo 1 || echo 0)" \
       "camera_off=$(grep -q '^#cz-off dtoverlay=imx219' "$CFG" && echo 1 || echo 0)" \
       "hdmi_off=$(grep -q "$HDMI_ARG" "$CMD" && echo 1 || echo 0)" \
       "cloudinit_off=$([ -e "$CLOUD_OFF" ] && echo 1 || echo 0)" \
       "modified=$([ -e "$ORIG_CFG" ] && echo 1 || echo 0)" \
       "boot_groups=${groups}"
}

set_boot_server() {  # $1 = 0|1, $2 = groups separated by spaces (empty = all)
  if [ "$1" = 1 ]; then
    cat > "$BOOT_UNIT.cz-tmp" <<EOF
[Unit]
Description=CardputerZero Server Mode at boot
After=network.target cups.service accounts-daemon.service rpcbind.service

[Service]
Type=oneshot
ExecStart=/usr/lib/cz-server-mode/server-mode.sh --boot $2
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF
    mv -f "$BOOT_UNIT.cz-tmp" "$BOOT_UNIT"
    systemctl daemon-reload
    systemctl enable cz-server-mode-boot.service >/dev/null 2>&1
    systemctl set-default multi-user.target >/dev/null 2>&1
  else
    [ -f "$BOOT_UNIT" ] || return 0
    systemctl disable cz-server-mode-boot.service >/dev/null 2>&1 || true
    rm -f "$BOOT_UNIT"
    systemctl daemon-reload
    systemctl set-default graphical.target >/dev/null 2>&1
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

apply() {
  need_root
  take_lock
  [ -e "$ORIG_CFG" ] || atomic_copy "$CFG" "$ORIG_CFG"
  [ -e "$ORIG_CMD" ] || atomic_copy "$CMD" "$ORIG_CMD"

  # edit working copies, then swap them in atomically
  local new_cfg=$CFG.cz-new new_cmd=$CMD.cz-new
  cp "$CFG" "$new_cfg"
  cp "$CMD" "$new_cmd"
  local do_reboot=0 boot_server="" groups="" kv k v
  for kv in "$@"; do
    [ "$kv" = --reboot ] && { do_reboot=1; continue; }
    k=${kv%%=*}; v=${kv#*=}
    case "$k" in
      gpu)
        case "$v" in 16|32|64|128) ;; *) echo "bad gpu value: $v" >&2; exit 2 ;; esac
        if grep -q '^gpu_mem=' "$new_cfg"; then
          sed -i "s/^gpu_mem=.*/gpu_mem=$v/" "$new_cfg"
        else
          [ -z "$(tail -c1 "$new_cfg")" ] || echo >> "$new_cfg"   # keep the last line intact
          echo "gpu_mem=$v" >> "$new_cfg"
        fi ;;
      camera_off)
        if [ "$v" = 1 ]; then sed -i 's/^dtoverlay=imx219/#cz-off dtoverlay=imx219/' "$new_cfg"
        else sed -i 's/^#cz-off dtoverlay=imx219/dtoverlay=imx219/' "$new_cfg"; fi ;;
      hdmi_off)
        if [ "$v" = 1 ]; then grep -q "$HDMI_ARG" "$new_cmd" || sed -i "1 s/\$/ $HDMI_ARG/" "$new_cmd"
        else sed -i "s/ $HDMI_ARG//g" "$new_cmd"; fi ;;
      boot_server) boot_server=$v ;;
      groups)
        groups=${v//,/ }
        for g in $groups; do group_units "$g" >/dev/null || { echo "unknown group: $g" >&2; exit 2; }; done ;;
      cloudinit_off)
        if [ "$v" = 1 ]; then touch "$CLOUD_OFF"; else rm -f "$CLOUD_OFF"; fi ;;
      *) echo "unknown key: $k" >&2; exit 2 ;;
    esac
  done
  cmp -s "$new_cfg" "$CFG" || atomic_copy "$new_cfg" "$CFG"
  cmp -s "$new_cmd" "$CMD" || atomic_copy "$new_cmd" "$CMD"
  rm -f "$new_cfg" "$new_cmd"

  # groups alone (boot_server already on) rewrite the unit with the new selection
  if [ -z "$boot_server" ] && [ -n "$groups" ] && [ -f "$BOOT_UNIT" ]; then boot_server=1; fi
  [ -n "$boot_server" ] && set_boot_server "$boot_server" "$groups"
  sync
  echo "applied: $(status)"
  [ $do_reboot = 1 ] && reboot_soon
  return 0
}

reset() {
  need_root
  take_lock
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
