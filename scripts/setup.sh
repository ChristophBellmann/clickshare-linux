#!/bin/sh
# One-time setup: creates a dedicated Wine prefix, extracts the ClickShare app
# from ClickShare_Setup.exe into it and builds the DXGI shim.
#
# Usage: sh scripts/setup.sh /path/to/ClickShare_Setup.exe
#
# Environment:
#   CLICKSHARE_PREFIX   Wine prefix to use (default: ~/.wine-clickshare)
set -e

DIR=$(cd "$(dirname "$0")/.." && pwd)
SETUP=${1:-$HOME/Downloads/ClickShare_Setup.exe}
export WINEPREFIX=${CLICKSHARE_PREFIX:-$HOME/.wine-clickshare}
# Keep Wine from offering Mono/Gecko installs; the app does not need them.
export WINEDLLOVERRIDES="mscoree,mshtml="
export WINEDEBUG=-all

if [ ! -f "$SETUP" ]; then
    echo "ClickShare_Setup.exe not found: $SETUP" >&2
    echo "Usage: sh scripts/setup.sh /path/to/ClickShare_Setup.exe" >&2
    exit 1
fi

if ! command -v wine >/dev/null 2>&1; then
    echo "Wine is not installed. Run: sudo sh scripts/install-winehq.sh" >&2
    exit 1
fi
WINE_MAJOR=$(wine --version | sed -E 's/^wine-([0-9]+).*/\1/')
if [ "$WINE_MAJOR" -lt 9 ] 2>/dev/null; then
    echo "Wine $(wine --version) is too old (need >= 9, tested with 11.0)." >&2
    echo "Run: sudo sh scripts/install-winehq.sh" >&2
    exit 1
fi

echo "== Wine prefix: $WINEPREFIX"
if [ ! -f "$WINEPREFIX/system.reg" ]; then
    wineboot -i
    wineserver -w
fi

echo "== Extracting ClickShare app"
LOCALAPPDATA=$(winepath -u "$(wine cmd /c echo %LOCALAPPDATA% 2>/dev/null | tr -d '\r')")
python3 -I "$DIR/scripts/extract-clickshare-app.py" "$SETUP" "$LOCALAPPDATA/ClickShare"

echo "== Building shim"
sh "$DIR/scripts/build.sh"

echo
echo "Setup complete. Plug in the ClickShare Button and run: sh clickshare.sh"
