#!/bin/bash
# Return from server mode: start again exactly what server-mode.sh stopped.
# Exit: 0 ok, 1 some unit failed to start (those stay in the state file for a retry), 3 busy.
set -u
# shellcheck source=common.sh
. "$(dirname "$(readlink -f "$0")")/common.sh"
need_root

take_lock
if [ ! -f "$STATE_FILE" ]; then
  echo "already in normal mode"
  exit 0
fi
before=$(mem_avail_mb)

# reverse order: sockets/helpers first, lightdm (recorded first) last
failed_units=""
for u in $(tac "$STATE_FILE"); do
  case " $ON_DEMAND_UNITS " in *" $u "*) continue ;; esac
  unit_exists "$u" || { echo "not installed, skipped: $u"; continue; }
  if systemctl start "$u"; then
    echo "started: $u"
  else
    echo "failed to start: $u" >&2
    # prepend: the file keeps the original order, so the next retry also runs in reverse
    failed_units=" $u$failed_units"
  fi
done

if [ -n "$failed_units" ]; then
  # shellcheck disable=SC2086  # word list: one unit per line
  printf '%s\n' $failed_units > "$STATE_FILE.tmp" && mv "$STATE_FILE.tmp" "$STATE_FILE"
  echo "normal mode incomplete, failed:$failed_units" >&2
  exit 1
fi
rm -f "$STATE_FILE"
echo "normal mode on. MemAvailable: ${before} MB -> $(mem_avail_mb) MB"
