#!/usr/bin/env bash
# Builds the Release VST3, installs it to ~/.vst3 and restarts REAPER on it.
#
#   ./build.sh               build, install, then quit and reopen REAPER (or open it)
#   ./build.sh --no-reaper   build and install only
#   ./build.sh --force       install and restart even if the plugin didn't change
#
# REAPER is asked to quit through its own File: Quit, so unsaved changes still
# get its save prompt; if that is cancelled REAPER stays open and this gives
# up. It reopens on the project that was active. Nothing is installed or
# restarted when the build fails or the plugin didn't change.

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
reaper_bin=/opt/REAPER/reaper
installed="$HOME/.vst3/3DQ.vst3"
so_path=Contents/x86_64-linux/3DQ.so
restart_reaper=1
force=0

for arg in "$@"; do
    case "$arg" in
        --no-reaper) restart_reaper=0 ;;
        --force) force=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

# The build. JUCE_VST3DESTDIR= stops make copying into ~/.vst3 itself.
make -C "$root/builds/linux_makefile" -j"$(nproc)" CONFIG=Release JUCE_VST3DESTDIR=
built="$root/builds/linux_makefile/build/3DQ.vst3"

if (( ! force )) && [[ -f "$installed/$so_path" ]] && cmp -s "$built/$so_path" "$installed/$so_path"; then
    echo "3DQ unchanged: nothing to install, REAPER left alone."
    exit 0
fi

reaper_running() { pgrep -xf "$reaper_bin.*" > /dev/null; }

project=""
if (( restart_reaper )) && reaper_running; then
    # A script run inside REAPER notes the active project, then quits through
    # File: Quit (action 40004) once the script has finished.
    # In the build folder rather than /tmp, which a sandboxed shell may not share with REAPER.
    work="$(mktemp -d "$root/builds/linux_makefile/build/reaper_restart.XXXXXX")"
    trap 'rm -rf "$work"' EXIT
    cat > "$work/quit.lua" <<EOF
local _, path = reaper.EnumProjects (-1)
local f = io.open ("$work/project.txt", "w")
if f then f:write (path or ""); f:close() end
reaper.defer (function() reaper.Main_OnCommand (40004, 0) end)
EOF
    echo "Asking REAPER to quit..."
    "$reaper_bin" -nonewinst "$work/quit.lua" > /dev/null 2>&1 &

    # Up to two minutes, for a save prompt to be answered.
    for _ in $(seq 240); do
        reaper_running || break
        sleep 0.5
    done

    if reaper_running; then
        echo "REAPER is still open (save prompt cancelled?): not installing over it. Run ./build.sh again." >&2
        exit 1
    fi
    project="$(cat "$work/project.txt" 2> /dev/null || true)"
fi

# Install through a temporary copy, so a half-copied plugin is never in place.
mkdir -p "$HOME/.vst3"
rm -rf "$HOME/.vst3/.3DQ.vst3.new"
cp -r "$built" "$HOME/.vst3/.3DQ.vst3.new"
rm -rf "$installed"
mv "$HOME/.vst3/.3DQ.vst3.new" "$installed"
cmp "$built/$so_path" "$installed/$so_path"
echo "Installed $installed"

if (( restart_reaper )) && ! reaper_running; then
    if [[ -n "$project" && -f "$project" ]]; then
        echo "Opening REAPER on $project"
        setsid "$reaper_bin" -nosplash "$project" > /dev/null 2>&1 < /dev/null &
    else
        echo "Opening REAPER"
        setsid "$reaper_bin" -nosplash > /dev/null 2>&1 < /dev/null &
    fi
fi
