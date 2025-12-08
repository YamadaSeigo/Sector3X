import os
import argparse
from pathlib import Path
import numpy as np
import trimesh
import pyVHACD
import struct

# ===== 引数パース: convert_textures.py と同じスタイル =====
parser = argparse.ArgumentParser()
parser.add_argument("--solution", required=True)
parser.add_argument("--config", required=True)  # Debug / Release
args = parser.parse_args()

SOLUTION_DIR = Path(args.solution).resolve()
PROJECT_DIR  = Path(__file__).resolve().parent
CONFIG       = args.config.lower()

# モデル入力フォルダ / 出力フォルダ
MODEL_DIR = SOLUTION_DIR / "assets" / "model"
OUT_DIR   = SOLUTION_DIR / "generated" / "convex"

# 入力元のフォルダを一応作成（なくてもOKだけど安全）
MODEL_DIR.mkdir(parents=True, exist_ok=True)
# 出力先フォルダ
OUT_DIR.mkdir(parents=True, exist_ok=True)

# 対象とする拡張子
SUPPORTED_EXT = {".glb", ".gltf", ".fbx", ".obj"}

# 対象とする拡張子
SUPPORTED_EXT = {".glb", ".gltf", ".fbx", ".obj"}

# Jolt の ConvexHull 頂点上限（推奨: 256）
MAX_VERTICES_PER_HULL = 256


def clamp_hull_vertex_count(points: np.ndarray,
                            faces_flat: np.ndarray,
                            max_vertices: int = MAX_VERTICES_PER_HULL):
    """
    1つの Hull について:
    - 使用されている頂点だけに絞る
    - max_vertices を超える場合は一部頂点だけ残して三角形をフィルタ
    - インデックスを 0..N-1 に張り替える

    戻り値:
        (new_points, new_faces_flat)
    """
    # 安全のためコピー＆型統一
    points = np.asarray(points, dtype=np.float64)
    faces_flat = np.asarray(faces_flat, dtype=np.uint32).reshape(-1)

    vcount = points.shape[0]
    if vcount <= max_vertices:
        # そのままでも制限内
        return points, faces_flat

    # (F,3) の三角形配列
    faces = faces_flat.reshape(-1, 3)

    # 実際に使われている頂点の集合
    used = np.unique(faces)

    if used.size <= max_vertices:
        # 使用頂点は 256 以下なので、使っている頂点だけに圧縮する
        keep = used
        faces_kept = faces
    else:
        # 使用頂点も 256 を超えている場合、「先頭 max_vertices 個」の頂点だけ残す
        # （幾何が多少変わるが、岩コリジョン用途なら実用上は十分なはず）
        keep = used[:max_vertices]

        # 三角形の3頂点が全て keep 内にあるものだけ残す
        mask = np.isin(faces, keep)
        tri_keep = np.all(mask, axis=1)
        faces_kept = faces[tri_keep]

        if faces_kept.size == 0:
            # 全ての三角形が消えてしまった場合:
            # 頂点だけ渡して物理側に凸包を作らせる想定（インデックスは空で返す）
            return points[keep], np.empty((0,), dtype=np.uint32)

    # old index -> new index のマップを作成
    remap = np.full(points.shape[0], -1, dtype=np.int64)
    remap[keep] = np.arange(keep.shape[0], dtype=np.int64)

    faces_mapped = remap[faces_kept]
    # ここで -1 が残っていたらバグ
    assert (faces_mapped >= 0).all()

    new_points = points[keep]
    new_faces_flat = faces_mapped.reshape(-1).astype(np.uint32)

    return new_points, new_faces_flat


# ===== モデル -> (points, faces_flat) ロード =====
def load_mesh(path: Path):
    mesh = trimesh.load(path, force='mesh')

    if not isinstance(mesh, trimesh.Trimesh):
        mesh = mesh.dump().sum()

    # trimesh は基本三角だが念のため
    # faces は (F, 3) なので flatten して M (= F*3) 要素の 1D にする
    verts = np.asarray(mesh.vertices, dtype=np.float64)
    faces = np.asarray(mesh.faces,    dtype=np.uint32)
    faces_flat = faces.reshape(-1)

    return verts, faces_flat

# ===== VHACD 実行（trimesh.convex_decomposition 経由版） =====
def run_vhacd(points: np.ndarray, faces_flat: np.ndarray):
    """
    Debug / Release で VHACD パラメータを切り替える例。
    ※ 前提:
      - trimesh >= 4.x
      - VHACD バックエンドがセットアップ済み（pyVHACD / testVHACD 等）
    """

    # (N,3), (M,) -> Trimesh に戻す
    faces = np.asarray(faces_flat, dtype=np.int64).reshape(-1, 3)
    mesh = trimesh.Trimesh(
        vertices=np.asarray(points, dtype=np.float64),
        faces=faces,
        process=False  # 余計な修正を避ける
    )

    # 共通の VHACD パラメータ
    # 岩用なので maxNumVerticesPerCH=256 にして Jolt の上限に合わせる
    common_params = dict(
        maxNumVerticesPerCH=MAX_VERTICES_PER_HULL,
    )

    # Debug / Release で切り替え
    if CONFIG == "debug":
        # 高精度 (解像度高め / ハル多め)
        vhacd_params = dict(
            maxConvexHulls=8,
            resolution=200_000,
            minimumVolumePercentErrorAllowed=0.5,
            **common_params,
        )
        print("[VHACD] Debug params:", vhacd_params)
    else:
        # Release 用：やや粗いけど高速
        vhacd_params = dict(
            maxConvexHulls=4,
            resolution=100_000,
            minimumVolumePercentErrorAllowed=2.0,
            **common_params,
        )
        print("[VHACD] Release params:", vhacd_params)

    # Trimesh の convex_decomposition は List[Trimesh] を返す
    hull_meshes = mesh.convex_decomposition(**vhacd_params)

    outputs = []
    for hm in hull_meshes:
        pts = np.asarray(hm.vertices, dtype=np.float64, order='C')
        fcs = np.asarray(hm.faces, dtype=np.uint32, order='C').reshape(-1)
        outputs.append((pts, fcs))

    return outputs


def pick_top_hulls(outputs, max_hulls: int):
    # 頂点数が多い Hull から max_hulls 個選ぶ
    outputs = sorted(outputs, key=lambda hf: hf[0].shape[0], reverse=True)
    return outputs[:max_hulls]

# ===== 独自バイナリ形式で出力 =====
def write_convex_binary(hulls, dst: Path):
    """
    フォーマット:
      char[4]  "CVXH"
      uint32   version = 1
      uint32   hull_count

      for hull in hulls:
          uint32 vertex_count
          uint32 index_count
          float32 x,y,z ... (vertex_count 個)
          uint32 i0,i1,i2... (index_count 個, 3の倍数)
    """
    dst.parent.mkdir(parents=True, exist_ok=True)

    with open(dst, "wb") as f:
        # ヘッダ
        f.write(b"CVXH")
        f.write(struct.pack("<I", 1))               # version
        f.write(struct.pack("<I", len(hulls)))      # hull_count

        for points, faces_flat in hulls:
            # Jolt の 256 頂点制限を満たすように再構成
            points, faces_flat = clamp_hull_vertex_count(points, faces_flat)

            vcount = int(points.shape[0])
            icount = int(faces_flat.size)

            f.write(struct.pack("<II", vcount, icount))

            # 頂点 float32 x,y,z...
            pts32 = points.astype(np.float32).reshape(-1)
            f.write(struct.pack(f"<{pts32.size}f", *pts32))

            # インデックス（デバッグ用途。Jolt 用には必須ではない）
            idx32 = faces_flat.astype(np.uint32).reshape(-1)
            f.write(struct.pack(f"<{idx32.size}I", *idx32))

    print(f"[VHACD] Wrote: {dst}")

# ===== 1ファイル処理（差分チェック込み） =====
def process_one_model(src: Path, max_hulls: int = 3):
    # モデルの assets/models からの相対パスを保ったまま出力
    rel = src.relative_to(MODEL_DIR)
    dst = OUT_DIR / rel.with_suffix(".chullbin")

    # 差分チェック
    if dst.exists():
        if dst.stat().st_mtime >= src.stat().st_mtime:
            print(f"[SKIP] {src} (up-to-date)")
            return

    print(f"[VHACD] Processing: {src}")
    points, faces_flat = load_mesh(src)
    vhacd_outputs = run_vhacd(points, faces_flat)

    if not vhacd_outputs:
        print(f"[WARN] VHACD result empty: {src}")
        return

    hulls = pick_top_hulls(vhacd_outputs, max_hulls)
    write_convex_binary(hulls, dst)

# ===== メイン =====
def main():
    # 岩用チューニング例:
    # Debug: 多めにハルを残して形状確認
    # Release: 少なめにして軽量化
    if CONFIG == "debug":
        max_hulls = 8
        print("[VHACD] MODE=Debug (max_hulls=8)")
    else:
        max_hulls = 3
        print("[VHACD] MODE=Release (max_hulls=3)")

    if not MODEL_DIR.exists():
        print(f"[VHACD] MODEL_DIR not found: {MODEL_DIR}")
        return

    for root, dirs, files in os.walk(MODEL_DIR):
        root_path = Path(root)
        for name in files:
            ext = Path(name).suffix.lower()
            if ext not in SUPPORTED_EXT:
                continue

            src = root_path / name
            process_one_model(src, max_hulls=max_hulls)

if __name__ == "__main__":
    main()
