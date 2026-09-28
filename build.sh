#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

echo "[*] Checking tools..."
for c in gcc ld objcopy python3 xxd; do
    command -v "$c" >/dev/null || { echo "MISSING: $c"; exit 1; }
done

[ -f linker.ld ] || { echo "ERROR: linker.ld missing"; exit 1; }
[ -d src ]       || { echo "ERROR: src/ missing"; exit 1; }
ls src/*.c >/dev/null 2>&1 || { echo "ERROR: no .c files in src/"; exit 1; }

# Font MUST bake before make — font_aa.c has .incbin "src/font_aa.bin"
echo "[*] Baking anti-aliased font"
python3 tools/bake_font.py

if [ -d image ]; then
    echo "[*] Baking UI assets from image/"
    python3 tools/bake_ui_assets.py || true
fi

echo "[*] Clean"
make clean

echo "[*] Building"
make -j"$(nproc)"

echo "[*] Hex"
make hex

echo ""
echo "[+] Built:"
ls -lh audioC0re.elf audioC0re.bin audioC0re.hex
echo ""
echo "Next: run  python3 audioC0re_launcher.py <PS5_IP>"
