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
# ※拡張子を除いた名前（stem）で判定するように変更
def choose_format(stem: str):
    lname = stem.lower()

    if lname.endswith(("_c", "_albedo", "_diffuse")):
        # カラー系 → sRGB
        return ["-f", "BC7_UNORM_SRGB", "-srgb"]
    elif lname.endswith(("_n", "_normal")):
        # 法線マップ → リニア (BC5)
        return ["-f", "BC5_UNORM"]
    elif lname.endswith(("_r", "_m", "_ao")):
        # Roughness/Metallic/AO → リニア (BC4)
        return ["-f", "BC4_UNORM"]
    elif lname.endswith(("_e", "_emissive")):
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

    # 拡張子を除いたファイル名で判定
    fmt_args = choose_format(src.stem)
    cmd = [
        str(TEXCONV),
        "-m", "0",        # 全ミップ生成
        "-y",             # 上書き許可
        "-nologo",        # ロゴ非表示
        #"-nogpu",         # GPU不使用
        "-o", str(dst_dir),
        str(src)
    ] + fmt_args

    print(f"[CONVERT] {src.name} -> {' '.join(fmt_args)}")
    subprocess.run(cmd, check=True)


def main():
    # 対象拡張子を増やす: PNG / JPG / JPEG
    exts = [".png", ".jpg", ".jpeg"]

    for ext in exts:
        # 再帰的に該当拡張子を探す
        for tex in ASSET_DIR.rglob(f"*{ext}"):
            rel_path = tex.relative_to(ASSET_DIR).parent
            dst_dir = OUT_DIR / rel_path
            dst_dir.mkdir(parents=True, exist_ok=True)

            convert_texture(tex, dst_dir)

if __name__ == "__main__":
    main()
