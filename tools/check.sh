#!/bin/bash
# Static checks for Server Mode. Run on the board after a build (needs build/cp0-native/compile_commands.json).
#   tools/check.sh              check the app files (src/view/screens, platform/linux_input, app/app.cpp)
#   tools/check.sh --all        check every file under src/
#   tools/check.sh --fix-format apply clang-format to the app files
# Tools: clang-format, clang-tidy, cppcheck, shellcheck. They are taken from PATH,
# or from a venv: python3 -m venv ~/.cache/cz-lint-venv && ~/.cache/cz-lint-venv/bin/pip install clang-format clang-tidy cppcheck
set -u
cd "$(dirname "$(readlink -f "$0")")/.." || exit 2
VENV=${CZ_LINT_VENV:-$HOME/.cache/cz-lint-venv}
[ -d "$VENV/bin" ] && PATH="$VENV/bin:$PATH"
BUILD=build/cp0-native

if [ "${1:-}" = --all ]; then
  mapfile -t FILES < <(find src -name '*.cpp' -not -path 'src/config/*' -not -name 'desktop_*' | sort)
else
  mapfile -t FILES < <(ls src/view/screens/{server_panel,advanced_panel,sudo_job,server_settings}.cpp \
    src/platform/linux_input.cpp src/app/app.cpp)
fi
HEADERS=(src/view/screens/{server_panel,advanced_panel,sudo_job,server_settings,panel_common}.h)

status=0
step() { printf '\n== %s\n' "$1"; }

step clang-format
if [ "${1:-}" = --fix-format ]; then
  clang-format -i "${FILES[@]}" "${HEADERS[@]}"
else
  clang-format --dry-run -Werror src/view/screens/*.{h,cpp} || status=1
fi

step clang-tidy
if [ -f "$BUILD/compile_commands.json" ]; then
  clang-tidy -p "$BUILD" --quiet "${FILES[@]}" 2>/dev/null | grep -E 'warning:|error:' -A3 && status=1
else
  echo "no $BUILD/compile_commands.json: build first"; status=1
fi

step "cppcheck (bugs, dead code)"
mkdir -p "$BUILD/cppcheck"
cppcheck --project="$BUILD/compile_commands.json" --enable=warning,style,performance,portability,unusedFunction \
  --cppcheck-build-dir="$BUILD/cppcheck" -i "$PWD/.cache" -i "$PWD/build" \
  --suppress="*:$PWD/.cache/*" --suppress=useStlAlgorithm \
  --suppress=missingIncludeSystem --inline-suppr --quiet --error-exitcode=1 \
  --std=c++17 --check-level=exhaustive -j2 2>&1 || status=1

step shellcheck
if command -v shellcheck >/dev/null; then
  shellcheck -x -P SCRIPTDIR scripts/*.sh tools/*.sh || status=1
else
  echo "shellcheck not installed: skipped"
fi

exit $status
