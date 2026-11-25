#!/bin/bash
# Switch CardputerZero to server mode: stop the desktop and extra services, keep the CardputerZero screen.
# Usage: server-mode.sh [--boot] [group...]   groups: desktop printing packagekit portals accounts nfs
# Nothing is uninstalled or disabled; reboot or normal-mode.sh brings everything back.
# Exit: 0 ok, 1 some unit failed to stop (state kept so normal-mode.sh can restore), 2 bad args, 3 busy.
set -u
. "$(dirname "$(readlink -f "$0")")/common.sh"
need_root

boot=0
if [ "${1:-}" = --boot ]; then boot=1; shift; fi
groups="$*"; [ -n "$groups" ] || groups=$ALL_GROUPS
for g in $groups; do group_units "$g" >/dev/null || { echo "unknown group: $g" >&2; exit 2; }; done

take_lock
if [ -f "$STATE_FILE" ]; then echo "already in server mode"; exit 0; fi
mkdir -p "$STATE_DIR"
before=$(mem_avail_mb)

units=""
for g in $groups; do
  for u in $(group_units "$g"); do
    # at boot into multi-user the desktop is not running yet, but normal mode must start it
    if systemctl is-active --quiet "$u" || { [ $boot = 1 ] && [ "$u" = lightdm.service ]; }; then
      units="$units $u"
    fi
  done
done
user_units=""
case " $groups " in *" portals "*)
  for u in $USER_UNITS; do
    user_systemctl is-active --quiet "$u" 2>/dev/null && user_units="$user_units $u"
  done ;;
esac

# state is written (atomically) before anything is stopped
printf '%s\n' $units > "$STATE_FILE.tmp" && mv "$STATE_FILE.tmp" "$STATE_FILE" \
  || { echo "cannot write $STATE_FILE" >&2; rm -f "$STATE_FILE.tmp"; exit 1; }

failed=0
for u in $units; do
  systemctl is-active --quiet "$u" || continue
  if systemctl stop "$u"; then echo "stopped: $u"; else echo "failed to stop: $u" >&2; failed=1; fi
done
for u in $user_units; do
  user_systemctl stop "$u" 2>/dev/null && echo "stopped (user): $u"
done

echo "server mode on. MemAvailable: ${before} MB -> $(mem_avail_mb) MB"
exit $failed
