import subprocess
from pathlib import Path
import argparse

# 実行時にソリューションディレクトリを渡す（convert_textures.py と同じ形式）
parser = argparse.ArgumentParser()
parser.add_argument("--solution", required=True)
parser.add_argument("--config", required=True)  # Debug / Release
parser.add_argument("--ffmpeg", default="ffmpeg")  # PATHに無ければフルパス指定
args = parser.parse_args()

SOLUTION_DIR = Path(args.solution).resolve()
PROJECT_DIR = Path(__file__).resolve().parent  # このスクリプト自身のあるディレクトリ
CONFIG = args.config

SRC_DIR = SOLUTION_DIR / "assets" / "audioSrc"
OUT_DIR = SOLUTION_DIR / "assets" / "audio"

# 入出力フォルダを作成
SRC_DIR.mkdir(parents=True, exist_ok=True)
OUT_DIR.mkdir(parents=True, exist_ok=True)

BGM_DIR_NAMES = {"bgm", "music"}
SE_DIR_NAMES  = {"se", "sfx", "sound", "sounds", "fx"}

# 対象入力拡張子（必要なら増やせます）
INPUT_EXTS = {".mp3"}

def is_bgm(rel: Path) -> bool:
    parts = [p.lower() for p in rel.parts]
    return any(p in BGM_DIR_NAMES for p in parts)

def is_se(rel: Path) -> bool:
    parts = [p.lower() for p in rel.parts]
    return any(p in SE_DIR_NAMES for p in parts)

def choose_out_ext(rel: Path) -> str:
    # ルール（必要ならここをあなたの運用に合わせて変更）
    # - SE系フォルダ -> wav（即再生）
    # - BGM系フォルダ -> ogg（ストリーミング）
    # - どちらでもない -> ogg
    if is_se(rel):
        return ".wav"
    return ".ogg"

def needs_convert(src: Path, dst: Path) -> bool:
    # convert_textures.py と同じ差分チェック（mtime）
    if not dst.exists():
        return True
    return dst.stat().st_mtime < src.stat().st_mtime

def ensure_ffmpeg_runnable(ffmpeg: str) -> None:
    try:
        subprocess.run([ffmpeg, "-version"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    except Exception:
        print(f"[ERR] ffmpeg not found or not runnable: {ffmpeg}")
        print("      Install ffmpeg and add to PATH, or pass --ffmpeg <fullpath>")
        raise SystemExit(3)

def convert_one(src: Path, dst: Path, ffmpeg: str) -> None:
    rel = src.relative_to(SRC_DIR)

    if not needs_convert(src, dst):
        print(f"[SKIP] {rel.as_posix()} (already up-to-date)")
        return

    dst.parent.mkdir(parents=True, exist_ok=True)

    # 出力拡張子でエンコードを切り替え
    if dst.suffix.lower() == ".ogg":
        # OGG Vorbis（BGM向け）: 品質 5 (0..10) 目安 4-6
        cmd = [
            ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
            "-i", str(src),
            "-vn",
            "-c:a", "libvorbis",
            "-q:a", "5",
            str(dst),
        ]
        print(f"[CONVERT] {rel.as_posix()} -> {dst.relative_to(OUT_DIR).as_posix()} (ogg q=5)")
        subprocess.run(cmd, check=True)
        return

    if dst.suffix.lower() == ".wav":
        # WAV PCM16（SE向け）: 44100Hz stereo（必要ならmonoに）
        cmd = [
            ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
            "-i", str(src),
            "-vn",
            "-c:a", "pcm_s16le",
            "-ar", "44100",
            "-ac", "2",
            str(dst),
        ]
        print(f"[CONVERT] {rel.as_posix()} -> {dst.relative_to(OUT_DIR).as_posix()} (wav pcm_s16le 44.1kHz)")
        subprocess.run(cmd, check=True)
        return

    print(f"[WARN] Unsupported output ext: {dst}")

def main():
    ensure_ffmpeg_runnable(args.ffmpeg)

    count_ok = 0
    count_skip = 0

    for p in SRC_DIR.rglob("*"):
        if not p.is_file():
            continue
        if p.suffix.lower() not in INPUT_EXTS:
            continue

        rel = p.relative_to(SRC_DIR)
        out_ext = choose_out_ext(rel)

        dst = (OUT_DIR / rel).with_suffix(out_ext)

        if needs_convert(p, dst):
            convert_one(p, dst, args.ffmpeg)
            count_ok += 1
        else:
            print(f"[SKIP] {rel.as_posix()} (already up-to-date)")
            count_skip += 1

    print(f"[DONE] converted={count_ok}, skip={count_skip}")

if __name__ == "__main__":
    main()
