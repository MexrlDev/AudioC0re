#!/usr/bin/env python3
"""
tools/bake_ui_assets.py — AudioC0re UI image baker.
"""
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("error: Pillow is required.  pip install Pillow")


def find_one(folder: Path, stem: str):
    for ext in (".png", ".jpg", ".jpeg"):
        p = folder / (stem + ext)
        if p.is_file():
            return p
    return None


def convert(src: Path, dst: Path, w: int, h: int) -> None:
    img = Image.open(src).convert("RGBA")
    iw, ih = img.size
    if (iw, ih) != (w, h):
        scale = min(w / iw, h / ih)
        nw = max(1, int(iw * scale))
        nh = max(1, int(ih * scale))
        img = img.resize((nw, nh), Image.LANCZOS)
        canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        canvas.paste(img, ((w - nw) // 2, (h - nh) // 2), img)
        img = canvas

    # PIL's split() on RGBA returns (R, G, B, A) as L-mode bands.
    # Rebuild with the R and B bands swapped so that tobytes() writes
    # bytes in BGRA order — the layout the C-side framebuffer expects.
    r_ch, g_ch, b_ch, a_ch = img.split()
    bgra = Image.merge("RGBA", (b_ch, g_ch, r_ch, a_ch))

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(bgra.tobytes())
    print(f"  {src.name:22s} -> {dst.relative_to(dst.parent.parent)}"
          f"  {w}x{h}  {len(bgra.tobytes()):,} B")


def main():
    root = Path(".").resolve()
    image_dir = root / "image"
    out_image = root / "out" / "image"
    out_cover = root / "out" / "cover"

    if not image_dir.exists():
        print("no image/ folder — skipping UI asset bake")
        return

    logo    = find_one(image_dir, "logo")
    loading = find_one(image_dir, "loading")
    default = find_one(image_dir, "default")

    if logo:    convert(logo, out_image / "logo.bin", 500, 500)
    else:       print("  no logo.* in image/ — splash will be skipped")

    if loading: convert(loading, out_image / "loading.bin", 200, 200)
    else:       print("  no loading.* in image/ — loading overlay will be text-only")

    if default: convert(default, out_cover / "_default.bin", 500, 500)
    else:       print("  no default.* in image/ — builder will use PIL placeholder")

    print("UI assets baked.")


if __name__ == "__main__":
    main()
