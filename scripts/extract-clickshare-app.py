#!/usr/bin/env python3
"""Extract the ClickShare desktop app from Barco's ClickShare_Setup.exe.

ClickShare_Setup.exe is a Squirrel.Windows installer. Running it under Wine
fails, because Squirrel's Update.exe needs the full .NET Framework and the
.NET web installer hangs. The actual app (clickshare_native.exe) is a native
Qt application that needs no .NET, so we unpack it directly:

    ClickShare_Setup.exe
      └─ embedded ZIP (PE resource)
           ├─ ClickShare-<version>-full.nupkg   (also a ZIP)
           │    └─ lib/net45/*                  <- the app
           ├─ RELEASES
           └─ Update.exe

Usage:
    extract-clickshare-app.py ClickShare_Setup.exe <target-dir>

The app ends up in <target-dir>/app-<version>/.
"""

import io
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

LOCAL_HEADER = b"PK\x03\x04"
END_OF_CENTRAL_DIR = b"PK\x05\x06"


def find_embedded_zip(data: bytes) -> bytes:
    """Return the ZIP archive that contains the .nupkg."""
    pos = data.find(LOCAL_HEADER)
    while pos != -1:
        name_len = struct.unpack_from("<H", data, pos + 26)[0]
        name = data[pos + 30 : pos + 30 + name_len]
        if name.endswith(b".nupkg") or name == b"RELEASES":
            eocd = data.find(END_OF_CENTRAL_DIR, pos)
            if eocd == -1:
                break
            comment_len = struct.unpack_from("<H", data, eocd + 20)[0]
            return data[pos : eocd + 22 + comment_len]
        pos = data.find(LOCAL_HEADER, pos + 1)
    sys.exit("error: no Squirrel package found in the given file")


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    setup, target = Path(sys.argv[1]), Path(sys.argv[2])

    outer = zipfile.ZipFile(io.BytesIO(find_embedded_zip(setup.read_bytes())))
    nupkg_name = next((n for n in outer.namelist() if n.endswith("-full.nupkg")), None)
    if not nupkg_name:
        sys.exit("error: no *-full.nupkg in the Squirrel package")

    match = re.search(r"-(\d+\.\d+\.\d+)", nupkg_name)
    version = match.group(1) if match else "unknown"
    app_dir = target / f"app-{version}"
    print(f"Found {nupkg_name} -> {app_dir}")

    nupkg = zipfile.ZipFile(io.BytesIO(outer.read(nupkg_name)))
    prefix = "lib/net45/"
    if app_dir.exists():
        shutil.rmtree(app_dir)
    app_dir.mkdir(parents=True)
    for name in nupkg.namelist():
        if not name.startswith(prefix) or name.endswith("/"):
            continue
        dest = app_dir / name[len(prefix) :]
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(nupkg.read(name))

    web = app_dir / "WebComponent.7z"
    if web.exists():
        seven = shutil.which("7z") or shutil.which("7za") or shutil.which("7zr")
        if seven:
            subprocess.run([seven, "x", "-y", f"-o{app_dir}", str(web)],
                           check=True, stdout=subprocess.DEVNULL)
        else:
            print("warning: 7z not found, WebComponent.7z was not unpacked "
                  "(install p7zip-full; screen sharing works without it)")

    if not (app_dir / "clickshare_native.exe").exists():
        sys.exit("error: clickshare_native.exe missing after extraction")
    print(f"ClickShare app {version} extracted.")


if __name__ == "__main__":
    main()
