import tkinter as tk
from tkinter import filedialog, messagebox
import subprocess
import os
import sys

# GUI
root = tk.Tk()
root.title("FBX → GLTF/GLB 変換ツール")

output_format = tk.StringVar(value="glb")

def get_extension(fmt):
    if fmt == "glb":
        return ".glb"
    else:
        return ".gltf"

def convert_fbx():
    input_path = filedialog.askopenfilename(filetypes=[("FBX files", "*.fbx")])
    if not input_path:
        return

    ext = get_extension(output_format.get())
    output_path = filedialog.asksaveasfilename(defaultextension=ext,
                                               filetypes=[("GLTF/GLB files", f"*{ext}")])
    if not output_path:
        return

    blender_path = "C:/Program Files/Blender Foundation/Blender 3.6/blender.exe"

    # Blenderスクリプトの絶対パスを取得（相対安全）
    base_dir = getattr(sys, '_MEIPASS', os.path.dirname(os.path.abspath(__file__)))
    script_path = os.path.join(base_dir, "fbx_to_gltf_script.py")

    fmt = output_format.get()  # "glb", "gltf_embed", "gltf_separate"
    cmd = [
        blender_path,
        "--background",
        "--python", script_path,
        "--", input_path, output_path, fmt
    ]

    try:
        subprocess.run(cmd, check=True)
        messagebox.showinfo("完了", f"変換完了:\n{output_path}")
    except subprocess.CalledProcessError as e:
        messagebox.showerror("エラー", f"変換に失敗:\n{e}")

tk.Label(root, text="出力形式を選択:").pack()

tk.Radiobutton(root, text="GLB（バイナリ1ファイル）", variable=output_format, value="glb").pack(anchor="w")
tk.Radiobutton(root, text="GLTF 埋め込み（.gltf）", variable=output_format, value="gltf_embed").pack(anchor="w")
tk.Radiobutton(root, text="GLTF 分離（.gltf + .bin + texture）", variable=output_format, value="gltf_separate").pack(anchor="w")

tk.Button(root, text="FBXを選んで変換", command=convert_fbx).pack(pady=20)

root.mainloop()
