import os
import argparse
from pathlib import Path
import numpy as np

import trimesh

try:
    from trimesh import repair
except Exception:
    repair = None

import struct

# ===== 引数パース: generate_convex.py と同じスタイル =====
parser = argparse.ArgumentParser()
parser.add_argument("--solution", required=True)
parser.add_argument("--config", required=True)  # Debug / Release
args = parser.parse_args()

SOLUTION_DIR = Path(args.solution).resolve()
PROJECT_DIR  = Path(__file__).resolve().parent
CONFIG       = args.config.lower()

# モデル入力フォルダ / 出力フォルダ
MODEL_DIR = SOLUTION_DIR / "assets" / "model"
OUT_DIR   = SOLUTION_DIR / "generated" / "meshshape"

MODEL_DIR.mkdir(parents=True, exist_ok=True)
OUT_DIR.mkdir(parents=True, exist_ok=True)

SUPPORTED_EXT = {".glb", ".gltf", ".fbx", ".obj"}

# ===== 目標トライアングル数のチューニング =====
# ある程度形を保ちながら、頂点数をできるだけ削減するための目安。
# （好みに合わせてここは調整してください）
TARGET_TRIS_DEBUG_MAX   = 8000   # Debug 時の最大三角形数
TARGET_TRIS_RELEASE_MAX = 3000   # Release 時の最大三角形数
TARGET_TRIS_RELEASE_MIN = 500    # これより少なくはしない（あまりにガタガタになるのを防ぐ）

def load_mesh(path: Path) -> trimesh.Trimesh:
    """
    ファイルを Trimesh としてロード。
    Scene の場合は dump().sum() で 1 mesh にまとめる。
    """
    mesh = trimesh.load(path, force="mesh")

    if not isinstance(mesh, trimesh.Trimesh):
        # Scene などの場合に 1 mesh に統合
        mesh = mesh.dump().sum()

    # watertight でなくても今回は OK（静的メッシュコリジョン用途）
    # if not mesh.is_watertight: ... は特に何もしないのでコメントアウトしても可

    # ---- クリーンアップ（使える関数だけ使う） ----
    # 1) 未参照頂点を削除
    if hasattr(mesh, "remove_unreferenced_vertices"):
        try:
            mesh.remove_unreferenced_vertices()
        except Exception as e:
            print(f"[Mesh][WARN] remove_unreferenced_vertices failed: {e}")

    # 2) 退化面の削除
    called = False
    if hasattr(mesh, "remove_degenerate_faces"):
        try:
            mesh.remove_degenerate_faces()
            called = True
        except Exception as e:
            print(f"[Mesh][WARN] mesh.remove_degenerate_faces failed: {e}")

    if not called and repair is not None and hasattr(repair, "remove_degenerate_faces"):
        try:
            repair.remove_degenerate_faces(mesh)
            called = True
        except Exception as e:
            print(f"[Mesh][WARN] repair.remove_degenerate_faces failed: {e}")

    if not called:
        # この trimesh バージョンには無い場合は普通にスキップ
        print("[Mesh][INFO] remove_degenerate_faces not available, skip")

    # 3) 重複面の削除
    called = False
    if hasattr(mesh, "remove_duplicate_faces"):
        try:
            mesh.remove_duplicate_faces()
            called = True
        except Exception as e:
            print(f"[Mesh][WARN] mesh.remove_duplicate_faces failed: {e}")

    if not called and repair is not None and hasattr(repair, "remove_duplicate_faces"):
        try:
            repair.remove_duplicate_faces(mesh)
            called = True
        except Exception as e:
            print(f"[Mesh][WARN] repair.remove_duplicate_faces failed: {e}")

    if not called:
        print("[Mesh][INFO] remove_duplicate_faces not available, skip")

    # 4) 無限値/NaN の削除
    called = False
    if hasattr(mesh, "remove_infinite_values"):
        try:
            mesh.remove_infinite_values()
            called = True
        except Exception as e:
            print(f"[Mesh][WARN] mesh.remove_infinite_values failed: {e}")

    if not called and repair is not None and hasattr(repair, "remove_infinite_values"):
        try:
            repair.remove_infinite_values(mesh)
            called = True
        except Exception as e:
            print(f"[Mesh][WARN] repair.remove_infinite_values failed: {e}")

    if not called:
        print("[Mesh][INFO] remove_infinite_values not available, skip")

    return mesh


def decide_target_triangles(orig_triangles: int) -> int:
    """
    元の三角形数から、目標三角形数を決める。
    Debug と Release で方針を変える。
    """
    if CONFIG == "debug":
        # Debug は形状確認重視：元の 60% か、最大 8000 の小さい方
        target = int(orig_triangles * 0.6)
        return max(1, min(target, TARGET_TRIS_DEBUG_MAX))
    else:
        # Release はより軽量：元の 25% を目安にしつつ、上限と下限を設ける
        target = int(orig_triangles * 0.25)
        target = max(TARGET_TRIS_RELEASE_MIN, min(target, TARGET_TRIS_RELEASE_MAX))
        return max(1, target)


def simplify_mesh(mesh: trimesh.Trimesh) -> trimesh.Trimesh:
    """
    Quadric Decimation を使って三角形数を削減する。
    失敗した場合は元メッシュを返す。
    """
    orig_tris = int(mesh.faces.shape[0])
    if orig_tris == 0:
        return mesh

    target_tris = decide_target_triangles(orig_tris)

    # そもそも元が十分少ないならそのまま
    if orig_tris <= target_tris:
        print(f"  [Mesh] Triangles already small ({orig_tris} <= {target_tris}), skip decimation.")
        return mesh

    print(f"  [Mesh] Simplify: {orig_tris} -> target ~ {target_tris} tris")

    try:
        simp = mesh.simplify_quadratic_decimation(target_tris)
        # 念のためクリーンアップ
        simp.remove_unreferenced_vertices()
        simp.remove_degenerate_faces()
        simp.remove_duplicate_faces()
        simp.remove_infinite_values()

        new_tris = int(simp.faces.shape[0])
        print(f"  [Mesh] Result: {new_tris} tris")
        if new_tris == 0:
            print("  [Mesh][WARN] simplification produced empty mesh, fallback to original.")
            return mesh

        return simp

    except Exception as e:
        print(f"  [Mesh][WARN] simplification failed: {e}")
        return mesh


def to_compact_arrays(mesh: trimesh.Trimesh):
    """
    Jolt MeshShape 用に、頂点とインデックスの配列を作る。
    - float32 の xyz
    - uint32 のインデックス（三角形リスト）
    """
    mesh.remove_unreferenced_vertices()

    verts = np.asarray(mesh.vertices, dtype=np.float32)
    faces = np.asarray(mesh.faces, dtype=np.uint32).reshape(-1, 3)

    # 念のためチェック
    if verts.shape[0] == 0 or faces.shape[0] == 0:
        return verts, np.empty((0,), dtype=np.uint32)

    faces_flat = faces.reshape(-1).astype(np.uint32)
    return verts, faces_flat


def write_mesh_binary(verts: np.ndarray, indices: np.ndarray, dst: Path):
    """
    独自バイナリフォーマットで書き出し。
    Jolt 側ではこのバイナリを読み込んで MeshShapeSettings を組み立てる想定。

    フォーマット:
      char[4] "JMSH"  // Jolt MeshShape
      uint32 version = 1

      uint32 vertex_count
      uint32 index_count   // 3 の倍数（三角形リスト）

      float32 x,y,z ... (vertex_count 個)
      uint32  i0,i1,i2...  (index_count 個)
    """
    dst.parent.mkdir(parents=True, exist_ok=True)

    vcount = int(verts.shape[0])
    icount = int(indices.size)

    with open(dst, "wb") as f:
        f.write(b"JMSH")
        f.write(struct.pack("<I", 1))         # version
        f.write(struct.pack("<II", vcount, icount))

        verts_flat = verts.reshape(-1).astype(np.float32)
        f.write(struct.pack(f"<{verts_flat.size}f", *verts_flat))

        idx_flat = indices.reshape(-1).astype(np.uint32)
        f.write(struct.pack(f"<{idx_flat.size}I", *idx_flat))

    print(f"  [Mesh] Wrote: {dst}  (v={vcount}, i={icount})")


def process_one_model(src: Path):
    """
    1つのモデルを処理して .meshbin を出力。
    差分チェックで「入力が新しくなったときだけ再生成」。
    """
    rel = src.relative_to(MODEL_DIR)
    dst = OUT_DIR / rel.with_suffix(".meshbin")

    # 差分チェック
    if dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime:
        print(f"[SKIP] {src} (up-to-date)")
        return

    print(f"[MeshShape] Processing: {src}")
    mesh = load_mesh(src)
    mesh = simplify_mesh(mesh)
    verts, indices = to_compact_arrays(mesh)

    if verts.shape[0] == 0 or indices.size == 0:
        print(f"[MeshShape][WARN] empty mesh after processing: {src}")
        return

    write_mesh_binary(verts, indices, dst)


def main():
    if not MODEL_DIR.exists():
        print(f"[MeshShape] MODEL_DIR not found: {MODEL_DIR}")
        return

    print(f"[MeshShape] MODE={CONFIG}")

    for root, dirs, files in os.walk(MODEL_DIR):
        root_path = Path(root)
        for name in files:
            ext = Path(name).suffix.lower()
            if ext not in SUPPORTED_EXT:
                continue
            src = root_path / name
            process_one_model(src)


if __name__ == "__main__":
    main()
