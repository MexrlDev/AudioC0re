#!/usr/bin/env python3
"""
tools/build_library.py — AudioC0re library builder.

Scans music/ and covers/, matches by filename stem, converts audio to
raw 48000 Hz stereo s16 PCM via ffmpeg, converts covers to 500x500 BGRA,
emits manifest.json + manifest.bin, stages everything into out/.

Cover priority for each track:
    1. covers/<stem>.{jpg,jpeg,png}                 (explicit file)
    2. Embedded art inside the audio file           (ID3 APIC / FLAC PICTURE)
    3. image/default.{jpg,jpeg,png}                 (global fallback)
    4. PIL-generated placeholder                    (last resort)

Metadata priority for title / artist / album:
    1. library.yaml override
    2. Embedded tag from the audio file             (via ffprobe)
    3. Derived from filename (title only; artist/album empty)
"""
import argparse, json, shutil, struct, subprocess, sys, time
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("error: Pillow is required.  pip install Pillow")

try:
    import yaml
except ImportError:
    yaml = None

AUDIO_EXTS   = {".mp3", ".flac", ".wav", ".m4a", ".aac", ".ogg", ".opus", ".wma"}
COVER_EXTS   = {".jpg", ".jpeg", ".png"}
DEFAULT_EXTS = (".png", ".jpg", ".jpeg")

MAGIC      = b"AC0R"
VERSION    = 2                    # v2 adds album_off, entry is 40 bytes
ENTRY_SIZE = 40
HEADER_SIZE = 32

COVER_W = 500
COVER_H = 500

VERBOSE = False


def vprint(*args):
    if VERBOSE:
        print(*args)


def stem_lower(p: Path) -> str:
    return p.stem.lower()


def find_audio(root: Path):
    out = {}
    for p in sorted(root.iterdir()):
        if p.is_file() and p.suffix.lower() in AUDIO_EXTS:
            out[stem_lower(p)] = p
    return out


def find_covers(root: Path):
    out = {}
    if not root.exists():
        return out
    for p in sorted(root.iterdir()):
        if p.is_file() and p.suffix.lower() in COVER_EXTS:
            out[stem_lower(p)] = p
    return out


def find_default(img_dir: Path):
    if not img_dir.exists():
        return None
    for ext in DEFAULT_EXTS:
        p = img_dir / ("default" + ext)
        if p.is_file():
            return p
    return None


def have_ffmpeg():  return shutil.which("ffmpeg")  is not None
def have_ffprobe(): return shutil.which("ffprobe") is not None


# ═══════════════════════════════════════════════════════════════════
# Metadata extraction
# ═══════════════════════════════════════════════════════════════════

def probe_metadata(audio: Path) -> dict:
    """
    Read title / artist / album tags from an audio file via ffprobe.
    Returns {'title': str, 'artist': str, 'album': str}.
    Missing fields come back as empty strings.  Never raises.
    """
    result = {"title": "", "artist": "", "album": ""}
    if not have_ffprobe():
        return result
    try:
        out = subprocess.check_output(
            ["ffprobe", "-v", "error",
             "-show_entries", "format_tags=title,artist,album",
             "-of", "default=noprint_wrappers=1",
             str(audio)],
            stderr=subprocess.DEVNULL,
        ).decode("utf-8", "replace")
    except subprocess.CalledProcessError:
        return result

    for line in out.splitlines():
        if "=" not in line:
            continue
        key, _, value = line.partition("=")
        key = key.strip().lower()
        value = value.strip()
        if not value:
            continue
        if key == "tag:title":
            result["title"] = value
        elif key == "tag:artist":
            result["artist"] = value
        elif key == "tag:album":
            result["album"] = value
    return result


def extract_embedded_cover(audio: Path, dst_png: Path) -> bool:
    """
    Pull the first attached image stream from an audio file via ffmpeg.
    Works for MP3 (ID3 APIC), FLAC (PICTURE block), M4A (covr atom),
    and most other containers.  Returns True on success.
    """
    if not have_ffmpeg():
        return False
    dst_png.parent.mkdir(parents=True, exist_ok=True)
    if dst_png.exists():
        dst_png.unlink()

    # -map 0:v? makes the video-stream mapping optional, so files
    # without embedded art don't error out; we just get no output.
    cmd = ["ffmpeg", "-y", "-loglevel", "error",
           "-i", str(audio),
           "-map", "0:v?",
           "-an",
           "-frames:v", "1",
           str(dst_png)]
    try:
        subprocess.run(cmd, check=True,
                       stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        return False
    return dst_png.exists() and dst_png.stat().st_size > 0


# ═══════════════════════════════════════════════════════════════════
# Cover conversion (PNG/JPEG -> 500x500 BGRA)
# ═══════════════════════════════════════════════════════════════════

def convert_cover(src: Path, dst: Path) -> None:
    img = Image.open(src).convert("RGBA")
    iw, ih = img.size
    if iw == 0 or ih == 0:
        raise RuntimeError(f"{src.name}: zero-sized image")

    scale = min(COVER_W / iw, COVER_H / ih)
    nw, nh = max(1, int(iw * scale)), max(1, int(ih * scale))
    img = img.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGBA", (COVER_W, COVER_H), (16, 16, 24, 255))
    canvas.paste(img, ((COVER_W - nw) // 2, (COVER_H - nh) // 2), img)

    r_ch, g_ch, b_ch, a_ch = canvas.split()
    bgra = Image.merge("RGBA", (b_ch, g_ch, r_ch, a_ch))
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(bgra.tobytes())


def make_placeholder(dst: Path, label: str) -> None:
    from PIL import ImageDraw, ImageFont
    img = Image.new("RGBA", (COVER_W, COVER_H), (32, 32, 48, 255))
    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.load_default()
    except Exception:
        font = None
    text = label[:28]
    tw = d.textlength(text, font=font) if font else len(text) * 6
    d.text(((COVER_W - tw) / 2, COVER_H / 2 - 6), text,
           fill=(200, 200, 220, 255), font=font)
    img.save(dst, "PNG")


# ═══════════════════════════════════════════════════════════════════
# Audio conversion (any format -> 48 kHz stereo s16 PCM)
# ═══════════════════════════════════════════════════════════════════

def probe_duration_ms(audio: Path) -> int:
    if not have_ffprobe():
        return 0
    try:
        out = subprocess.check_output([
            "ffprobe", "-v", "error",
            "-show_entries", "format=duration",
            "-of", "default=noprint_wrappers=1:nokey=1",
            str(audio),
        ], stderr=subprocess.DEVNULL).decode().strip()
        return int(float(out) * 1000)
    except Exception:
        return 0


def convert_audio(src: Path, dst: Path) -> bool:
    if not have_ffmpeg():
        print("  [!] ffmpeg is required to convert audio")
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-i", str(src),
           "-ar", "48000", "-ac", "2",
           "-f", "s16le", "-acodec", "pcm_s16le",
           str(dst)]
    try:
        subprocess.run(cmd, check=True,
                       stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
        return dst.exists() and dst.stat().st_size > 0
    except subprocess.CalledProcessError as e:
        print(f"  [!] ffmpeg failed on {src.name}: {e}")
        return False


# ═══════════════════════════════════════════════════════════════════
# Cover source resolution
# ═══════════════════════════════════════════════════════════════════

def cover_source_for(stem: str, audio: Path, covers: dict,
                     default_path, out_dir: Path, stats: dict) -> None:
    out_path = out_dir / f"{stem}.bin"

    # ---- Priority 1: explicit file in covers/ --------------------
    if stem in covers:
        src = covers[stem]
        try:
            convert_cover(src, out_path)
            stats["from_file"] += 1
            return
        except Exception as e:
            print(f"  [!] covers/{src.name}: {e}")
            stats["bad_cover"] += 1

    # ---- Priority 2: embedded art in the audio file --------------
    tmp_emb = out_dir / f"_emb_{stem}.png"
    if extract_embedded_cover(audio, tmp_emb):
        try:
            convert_cover(tmp_emb, out_path)
            tmp_emb.unlink(missing_ok=True)
            stats["from_embedded"] += 1
            return
        except Exception as e:
            print(f"  [!] embedded art from {audio.name}: {e}")
            tmp_emb.unlink(missing_ok=True)

    # ---- Priority 3: global default ------------------------------
    if default_path is not None:
        try:
            convert_cover(default_path, out_path)
            stats["default"] += 1
            return
        except Exception as e:
            print(f"  [!] image/{default_path.name}: {e}")

    # ---- Priority 4: generated placeholder -----------------------
    tmp_png = out_dir / f"_ph_{stem}.png"
    try:
        make_placeholder(tmp_png, stem)
        convert_cover(tmp_png, out_path)
        tmp_png.unlink(missing_ok=True)
        stats["placeholder"] += 1
    except Exception as e:
        print(f"  [!] placeholder failed for '{stem}': {e}")
        flat = bytearray(COVER_W * COVER_H * 4)
        for i in range(COVER_W * COVER_H):
            flat[i * 4 + 0] = 48
            flat[i * 4 + 1] = 32
            flat[i * 4 + 2] = 32
            flat[i * 4 + 3] = 255
        out_path.write_bytes(bytes(flat))
        stats["placeholder"] += 1


# ═══════════════════════════════════════════════════════════════════
# Manifest emission
# ═══════════════════════════════════════════════════════════════════

def emit_manifest_json(entries, out_path: Path):
    doc = {
        "format": "audioC0re-library",
        "version": VERSION,
        "generated_ts": int(time.time()),
        "cover_w": COVER_W, "cover_h": COVER_H,
        "count": len(entries),
        "entries": entries,
    }
    out_path.write_text(json.dumps(doc, indent=2))


def emit_manifest_bin(entries, out_path: Path):
    """
    Header (32 bytes, little-endian):
        0x00  char[4]  magic        "AC0R"
        0x04  u32      version      (2)
        0x08  u32      count
        0x0C  u32      cover_w
        0x10  u32      cover_h
        0x14  u32      strtab_off
        0x18  u32      strtab_size
        0x1C  u32      reserved

    Entry (40 bytes):
        0x00  u32  title_off
        0x04  u32  artist_off
        0x08  u32  album_off
        0x0C  u32  cover_off
        0x10  u32  audio_off
        0x14  u32  duration_ms
        0x18  u32  added_ts
        0x1C  u32  sort_key
        0x20  u32  flags
        0x24  u32  reserved
    """
    strtab = bytearray()
    offsets = []
    for e in entries:
        quad = []
        for s in (e["title"], e["artist"], e["album"],
                  e["cover"], e["audio"]):
            quad.append(len(strtab))
            strtab += s.encode("utf-8") + b"\x00"
        offsets.append(tuple(quad))

    entries_size = ENTRY_SIZE * len(entries)
    strtab_off   = HEADER_SIZE + entries_size

    buf = bytearray()
    buf += MAGIC
    buf += struct.pack("<I", VERSION)
    buf += struct.pack("<I", len(entries))
    buf += struct.pack("<I", COVER_W)
    buf += struct.pack("<I", COVER_H)
    buf += struct.pack("<I", strtab_off)
    buf += struct.pack("<I", len(strtab))
    buf += struct.pack("<I", 0)

    for e, (t, a, al, c, au) in zip(entries, offsets):
        buf += struct.pack("<I", t)
        buf += struct.pack("<I", a)
        buf += struct.pack("<I", al)
        buf += struct.pack("<I", c)
        buf += struct.pack("<I", au)
        buf += struct.pack("<I", e["duration_ms"])
        buf += struct.pack("<I", e["added_ts"])
        buf += struct.pack("<I", e["sort_key"])
        buf += struct.pack("<I", e.get("flags", 0))
        buf += struct.pack("<I", 0)

    buf += strtab
    out_path.write_bytes(buf)

    expected = HEADER_SIZE + ENTRY_SIZE * len(entries) + len(strtab)
    if len(buf) != expected:
        print(f"[!] manifest size mismatch: {len(buf)} vs {expected}")


# ═══════════════════════════════════════════════════════════════════
# Entry point
# ═══════════════════════════════════════════════════════════════════

def main():
    global VERBOSE

    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--out", default="out")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    VERBOSE = args.verbose
    root       = Path(args.root).resolve()
    out        = (root / args.out).resolve()
    music_dir  = root / "music"
    covers_dir = root / "covers"
    img_dir    = root / "image"
    yaml_path  = root / "library.yaml"

    if not music_dir.exists():
        sys.exit(f"error: {music_dir} does not exist")

    overrides = {}
    defaults  = {"sort": "added_desc", "title_case": "as_is"}
    if yaml_path.exists() and yaml is not None:
        doc = yaml.safe_load(yaml_path.read_text()) or {}
        defaults.update(doc.get("defaults", {}) or {})
        overrides = doc.get("overrides", {}) or {}

    def override_for(stem):
        for k, v in overrides.items():
            if k.lower() == stem:
                return v or {}
        return {}

    tracks = find_audio(music_dir)
    covers = find_covers(covers_dir)
    if not tracks:
        sys.exit(f"error: no audio files in {music_dir}")

    default_path = find_default(img_dir)

    for sub in ("cover", "audio"):
        p = out / sub
        if p.exists():
            shutil.rmtree(p)
        p.mkdir(parents=True, exist_ok=True)

    entries = []
    skipped = []
    stats = {
        "from_file": 0,
        "from_embedded": 0,
        "default": 0,
        "placeholder": 0,
        "bad_cover": 0,
    }

    if default_path:
        print(f"  default cover: {default_path.name}")

    # Pre-flight: reject any cover file PIL can't open
    bad_covers = set()
    for stem, p in covers.items():
        try:
            Image.open(p).verify()
        except Exception:
            bad_covers.add(stem)
            print(f"  [!] covers/{p.name}: not a valid image")

    if not have_ffprobe():
        print("  [i] ffprobe not found — embedded tags won't be read")
    if not have_ffmpeg():
        print("  [i] ffmpeg not found — embedded covers won't be extracted")

    for stem, audio in tracks.items():
        ov = override_for(stem)
        if ov.get("hidden"):
            skipped.append(stem)
            continue

        # ---- metadata --------------------------------------------
        meta = probe_metadata(audio)

        title = ov.get("title") or meta["title"]
        artist = ov.get("artist") or meta["artist"]
        album  = ov.get("album")  or meta["album"]

        if not title:
            title = stem.replace("_", " ").replace("-", " ").strip()
            if defaults.get("title_case") == "title":
                title = title.title()
            elif defaults.get("title_case") == "upper":
                title = title.upper()

        # ---- cover -------------------------------------------------
        effective_covers = {k: v for k, v in covers.items()
                            if k not in bad_covers}

        cover_source_for(stem, audio, effective_covers, default_path,
                         out / "cover", stats)

        # ---- audio -------------------------------------------------
        audio_out = out / "audio" / f"{stem}.pcm"
        print(f"  converting {audio.name} ...", end=" ", flush=True)
        if not convert_audio(audio, audio_out):
            print("FAILED")
            continue
        sz = audio_out.stat().st_size
        print(f"{sz:,} B")

        dur   = probe_duration_ms(audio)
        added = int(audio.stat().st_mtime)
        sort_key = int(ov.get("sort", 0)) if "sort" in ov else 0

        entries.append({
            "stem":        stem,
            "title":       title,
            "artist":      artist,
            "album":       album,
            "cover":       f"cover/{stem}.bin",
            "audio":       f"audio/{stem}.pcm",
            "duration_ms": dur,
            "added_ts":    added,
            "sort_key":    sort_key,
        })

    # ---- sort -----------------------------------------------------
    mode = defaults.get("sort", "added_desc")
    if mode == "added_desc":
        entries.sort(key=lambda e: e["added_ts"], reverse=True)
    elif mode == "added_asc":
        entries.sort(key=lambda e: e["added_ts"])
    elif mode == "title_asc":
        entries.sort(key=lambda e: e["title"].lower())
    elif mode == "duration_desc":
        entries.sort(key=lambda e: e["duration_ms"], reverse=True)
    # Manual sort_key pins first (lower values first; 0 = unpinned → end)
    entries.sort(key=lambda e: (e["sort_key"] == 0, e["sort_key"]))

    out.mkdir(parents=True, exist_ok=True)
    emit_manifest_json(entries, out / "manifest.json")
    emit_manifest_bin(entries, out / "manifest.bin")

    if not args.quiet:
        print(f"AudioC0re library built -> {out}")
        print(f"  tracks discovered:     {len(tracks)}")
        print(f"  hidden (skipped):      {len(skipped)}")
        print(f"  entries in manifest:   {len(entries)}")
        print(f"  covers from covers/:   {stats['from_file']}")
        print(f"  covers from tags:      {stats['from_embedded']}")
        print(f"  covers default:        {stats['default']}")
        print(f"  covers placeholder:    {stats['placeholder']}")
        if stats["bad_cover"]:
            print(f"  bad covers (skipped):  {stats['bad_cover']}")
        print(f"  manifest.bin:          "
              f"{(out/'manifest.bin').stat().st_size:,} B")


if __name__ == "__main__":
    main()
