#!/bin/sh
# Starts the ClickShare app under Wine with the DXGI Desktop Duplication shim
# injected, and routes system audio to the ClickShare Button.
#
# Usage: sh clickshare.sh
#
# Environment:
#   CLICKSHARE_PREFIX   Wine prefix to use (default: ~/.wine-clickshare)
#   CLICKSHARE_NO_AUDIO set to 1 to leave the audio output unchanged
DIR=$(cd "$(dirname "$0")" && pwd)
export WINEPREFIX=${CLICKSHARE_PREFIX:-$HOME/.wine-clickshare}
export WINEDLLOVERRIDES="mscoree,mshtml="
export WINEDEBUG=${WINEDEBUG:--all}

LOCALAPPDATA=$(winepath -u "$(wine cmd /c echo %LOCALAPPDATA% 2>/dev/null | tr -d '\r')")
APP=$(ls -d "$LOCALAPPDATA"/ClickShare/app-*/clickshare_native.exe 2>/dev/null | sort -V | tail -n 1)
if [ -z "$APP" ]; then
    echo "ClickShare app not found in $WINEPREFIX. Run scripts/setup.sh first." >&2
    exit 1
fi
if [ ! -f "$DIR/build/csdup.dll" ] || [ ! -f "$DIR/build/inject.exe" ]; then
    echo "Shim not built. Run: sh scripts/build.sh" >&2
    exit 1
fi

# The Button is a USB audio device. It forwards audio to the room only while
# a share is running, so it can stay the default output for the whole session.
if [ "${CLICKSHARE_NO_AUDIO:-0}" != 1 ] && command -v pactl >/dev/null 2>&1; then
    SINK=$(pactl list short sinks | awk '/Barco_ClickShare/ {print $2; exit}')
    if [ -n "$SINK" ]; then
        pactl set-default-sink "$SINK"
        pactl set-sink-mute "$SINK" 0
        for i in $(pactl list short sink-inputs | cut -f1); do
            pactl move-sink-input "$i" "$SINK" 2>/dev/null
        done
        echo "Audio output -> $SINK"
    else
        echo "ClickShare Button audio device not found; audio output unchanged."
    fi
fi

wineserver -k 2>/dev/null
exec wine "$DIR/build/inject.exe" "$(winepath -w "$APP")" "$(winepath -w "$DIR/build/csdup.dll")" 3000
