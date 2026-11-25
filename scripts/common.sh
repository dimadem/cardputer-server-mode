# shared settings for the Server Mode helper scripts (sourced, not run)
# shellcheck shell=bash disable=SC2034  # the variables are used by the scripts that source this file
umask 022   # the app runs without root and must read the state file whatever umask sudo passes
STATE_DIR=/run/cz-server-mode          # tmpfs: a reboot always forgets the runtime state
STATE_FILE=$STATE_DIR/stopped-units
LOCK_FILE=/run/cz-server-mode.lock

# Groups the app can ask server-mode.sh to stop (all of them when none is given).
# Keep in sync with settings::kStopGroups in src/view/screens/server_settings.h.
# Always kept: APPLaunch (CardputerZero screen), bluetooth (the launcher needs it),
# NetworkManager/wpa_supplicant (Wi-Fi), ssh, avahi (.local names), pipewire (audio), udisks2 (Files).
ALL_GROUPS="desktop printing packagekit portals accounts nfs"

group_units() {
  case "$1" in
    desktop)    echo "lightdm.service wayvnc-control.service" ;;   # whole Raspberry Pi desktop + HDMI output
    printing)   echo "cups.service cups-browsed.service cups.socket cups.path" ;;
    packagekit) echo "packagekit.service" ;;
    accounts)   echo "accounts-daemon.service" ;;
    nfs)        echo "rpcbind.service rpcbind.socket nfs-blkmap.service" ;;
    portals)    echo "" ;;                                          # user units, see USER_UNITS
    *) return 1 ;;
  esac
}

# User units left behind by the desktop session (group "portals"). They are
# D-Bus activated, so normal mode does not start them: the desktop does on demand.
USER_UNITS="xdg-desktop-portal.service xdg-desktop-portal-gtk.service xdg-desktop-portal-wlr.service xdg-document-portal.service xdg-permission-store.service gvfs-daemon.service gvfs-afc-volume-monitor.service gvfs-goa-volume-monitor.service gvfs-gphoto2-volume-monitor.service gvfs-mtp-volume-monitor.service gvfs-udisks2-volume-monitor.service"

# System units started on demand (D-Bus): normal mode does not start them by hand.
ON_DEMAND_UNITS="packagekit.service"

unit_exists() { systemctl cat "$1" >/dev/null 2>&1; }

need_root() { [ "$(id -u)" = 0 ] || { echo "run as root (sudo)" >&2; exit 1; }; }

# One switch at a time: the app, the boot unit and ssh sessions all go through this lock.
take_lock() {
  exec 9>"$LOCK_FILE"
  flock -n 9 || { echo "busy: another switch is running" >&2; exit 3; }
}

# the desktop/launcher user: SUDO_USER, else the owner of the APPLaunch process (may be empty at boot)
target_user() {
  if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ]; then echo "$SUDO_USER"; return; fi
  local pid; pid=$(pgrep -f '^/usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch' | head -1)
  [ -n "$pid" ] && ps -o user= -p "$pid" | tr -d ' '
}

# systemctl --user for the target user (resolved once per script run)
user_systemctl() {
  if [ -z "${TARGET_UID:-}" ]; then
    TARGET_USER=$(target_user); [ -n "$TARGET_USER" ] || return 1
    TARGET_UID=$(id -u "$TARGET_USER") || return 1
  fi
  runuser -u "$TARGET_USER" -- env XDG_RUNTIME_DIR="/run/user/$TARGET_UID" \
    DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$TARGET_UID/bus" systemctl --user "$@"
}

mem_avail_mb() { awk '/MemAvailable/ {print int($2/1024)}' /proc/meminfo; }
