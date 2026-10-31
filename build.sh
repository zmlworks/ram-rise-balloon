#!/bin/sh
# RamRiseBalloon - Windows x64 build with MinGW-w64 cross toolchain (Debian/Ubuntu: gcc-mingw-w64-x86-64)
#   ./build.sh        -> RamRiseBalloon.exe
#   ./build.sh zip    -> also out/ram-rise-balloon-vX.Y.Z-win64.zip (exe + README.md + LICENSE)
set -eu
cd "$(dirname "$0")"
VER=0.2.0
CC=${CC:-x86_64-w64-mingw32-gcc}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
( cd res && $WINDRES -O coff -i app.rc -o ../app.res.o )
$CC -O2 -Wall -Wextra -std=c11 -municode -mwindows -static -s \
    -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va \
    -o RamRiseBalloon.exe ramrise.c app.res.o \
    -lcomctl32 -lshell32 -ladvapi32 -lgdi32 -luser32 -lpsapi -lkernel32
rm -f app.res.o
echo "built RamRiseBalloon.exe ($(stat -c %s RamRiseBalloon.exe) bytes)"
if [ "${1:-}" = "zip" ]; then
    OUT=${OUT:-../out}
    mkdir -p "$OUT"
    Z="$OUT/ram-rise-balloon-v$VER-win64.zip"
    rm -f "$Z"
    if command -v zip >/dev/null 2>&1; then
        zip -q -X -j "$Z" RamRiseBalloon.exe README.md LICENSE
    else
        python3 -c 'import sys, zipfile
z = zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED)
for f in sys.argv[2:]: z.write(f, f)
z.close()' "$Z" RamRiseBalloon.exe README.md LICENSE
    fi
    echo "packed $Z"
fi
