import os
import subprocess
from pathlib import Path
import argparse
import sys
import shutil

# 実行時にソリューションディレクトリを渡す
parser = argparse.ArgumentParser()
parser.add_argument("--solution", required=True)
parser.add_argument("--config", required=True)  # Debug / Release が入る
args = parser.parse_args()

SOLUTION_DIR = Path(args.solution).resolve()
# このスクリプト自身のあるディレクトリを基準にする
PROJECT_DIR = Path(__file__).resolve().parent  # 例: AssetBuild/convert_textures.py → .. でプロジェクトルート

CONFIG = args.config

ASSET_DIR   = SOLUTION_DIR / "assets"
OUT_DIR     = SOLUTION_DIR / "converted" / "textures"

if CONFIG.lower() == "debug":
    TEXCONV     = PROJECT_DIR / "Tools" / "MDdx64" / "texconv.exe"
else:
    TEXCONV     = PROJECT_DIR / "Tools" / "MDx64" / "texconv.exe"

# 入力元のフォルダを一応作成
ASSET_DIR.mkdir(parents=True, exist_ok=True)

# 出力先フォルダを作成
OUT_DIR.mkdir(parents=True, exist_ok=True)

# ルール: ファイル名のサフィックスごとにフォーマットを決定
def choose_format(fname: str):
    lname = fname.lower()

    if lname.endswith(("_c.png", "_albedo.png", "_diffuse.png")):
        # カラー系 → sRGB
        return ["-f", "BC7_UNORM_SRGB", "-srgb"]
    elif lname.endswith(("_n.png", "_normal.png")):
        # 法線マップ → リニア (BC5)
        return ["-f", "BC5_UNORM"]
    elif lname.endswith(("_r.png", "_m.png", "_ao.png")):
        # Roughness/Metallic/AO → リニア (BC4)
        return ["-f", "BC4_UNORM"]
    elif lname.endswith(("_e.png", "_emissive.png")):
        # Emissive → sRGB
        return ["-f", "BC7_UNORM_SRGB", "-srgb"]
    else:
        # デフォルト（無指定は sRGB BC7）
        return ["-f", "BC7_UNORM_SRGB", "-srgb"]

def convert_texture(src: Path, dst_dir: Path):
    dst_file = dst_dir / (src.stem + ".dds")

    # --- 差分チェック ---
    if dst_file.exists():
        src_mtime = src.stat().st_mtime
        dst_mtime = dst_file.stat().st_mtime
        if dst_mtime >= src_mtime:
            print(f"[SKIP] {src.name} (already up-to-date)")
            return  # スキップ

    fmt_args = choose_format(src.name)
    cmd = [
        str(TEXCONV),
        "-m", "0",        # 全ミップ生成
        "-y",             # 上書き許可
        "-nologo",        # ロゴ非表示
        "-nogpu",         # GPU不使用
        "-o", str(dst_dir),
        str(src)
    ] + fmt_args

    print(f"[CONVERT] {src.name} -> {' '.join(fmt_args)}")
    subprocess.run(cmd, check=True)


def main():
    # 再帰的に PNG を探す
    for png in ASSET_DIR.rglob("*.png"):
        rel_path = png.relative_to(ASSET_DIR).parent
        dst_dir = OUT_DIR / rel_path
        dst_dir.mkdir(parents=True, exist_ok=True)

        convert_texture(png, dst_dir)

if __name__ == "__main__":
    main()
