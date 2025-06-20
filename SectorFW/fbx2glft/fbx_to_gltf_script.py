import bpy
import sys
import os

argv = sys.argv
argv = argv[argv.index("--") + 1:]

input_path = os.path.abspath(argv[0])
output_path = os.path.abspath(argv[1])
format_str = argv[2].lower()

format_map = {
    "glb": "GLB",
    "gltf_embed": "GLTF_EMBEDDED",
    "gltf_separate": "GLTF_SEPARATE"
}
export_format = format_map.get(format_str, "GLB")

# 出力ディレクトリとテクスチャサブフォルダ
output_dir = os.path.dirname(output_path)
texture_dir = "Textures"

print(f"[INFO] Exporting to {output_path} as {export_format}, textures in {texture_dir}")

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=input_path)

bpy.ops.export_scene.gltf(
    filepath=output_path,
    export_format=export_format,
    export_apply=True,
    export_yup=True,
    export_texcoords=True,
    export_normals=True,
    export_materials='EXPORT',
    export_animations=True,
    export_skins=True,
    export_image_format='AUTO',  # ← PNGがなければAUTOで代用
    export_texture_dir=texture_dir
)
