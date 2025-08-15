# generate_key_to_vk.py

# Key enum 順序（これに合わせて配列生成）
key_enum_order = [
    "Unknown",
    "A", "B", "C", "D", "E", "F", "G", "H", "I",
    "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S",
    "T", "U", "V", "W", "X", "Y", "Z",
    "Num0", "Num1", "Num2", "Num3", "Num4", "Num5", "Num6", "Num7", "Num8", "Num9",
    "Escape", "Enter", "Tab", "Backspace", "Space",
    "Left", "Right", "Up", "Down",
    "LCtrl", "RCtrl", "LShift", "RShift", "LAlt", "RAlt"
]

# Key → VKコード 対応表
key_to_vk = {
    "A": "0x41",  # 'A'
    "B": "0x42",
    "C": "0x43",
    "D": "0x44",
    "E": "0x45",
    "F": "0x46",
    "G": "0x47",
    "H": "0x48",
    "I": "0x49",
    "J": "0x4A",
    "K": "0x4B",
    "L": "0x4C",
    "M": "0x4D",
    "N": "0x4E",
    "O": "0x4F",
    "P": "0x50",
    "Q": "0x51",
    "R": "0x52",
    "S": "0x53",
    "T": "0x54",
    "U": "0x55",
    "V": "0x56",
    "W": "0x57",
    "X": "0x58",
    "Y": "0x59",
    "Z": "0x5A",

    "Num0": "0x30",
    "Num1": "0x31",
    "Num2": "0x32",
    "Num3": "0x33",
    "Num4": "0x34",
    "Num5": "0x35",
    "Num6": "0x36",
    "Num7": "0x37",
    "Num8": "0x38",
    "Num9": "0x39",

    "Escape": "VK_ESCAPE",
    "Enter": "VK_RETURN",
    "Tab": "VK_TAB",
    "Backspace": "VK_BACK",
    "Space": "VK_SPACE",

    "Left": "VK_LEFT",
    "Right": "VK_RIGHT",
    "Up": "VK_UP",
    "Down": "VK_DOWN",

    "LCtrl": "VK_LCONTROL",
    "RCtrl": "VK_RCONTROL",
    "LShift": "VK_LSHIFT",
    "RShift": "VK_RSHIFT",
    "LAlt": "VK_LMENU",
    "RAlt": "VK_RMENU"
}

def generate_key_to_vk_array():
    lines = []
    lines.append("constexpr std::array<int, static_cast<size_t>(Key::Count)> KeyToVKMap = [] {")
    lines.append("    std::array<int, static_cast<size_t>(Key::Count)> map{};")
    for key in key_enum_order:
        vk = key_to_vk.get(key, "0")  # 未定義は 0（Unknown）
        lines.append(f"    map[static_cast<size_t>(Key::{key})] = {vk};")
    lines.append("    return map;")
    lines.append("}();")
    return "\n".join(lines)

def generate_get_function():
    return (
        "inline int GetVKFromKey(Key key) {\n"
        "    return KeyToVKMap[static_cast<size_t>(key)];\n"
        "}"
    )

def main():
    print("// --- Key to WinAPI VK Mapping ---")
    print(generate_key_to_vk_array())
    print()
    print("// --- Lookup Function ---")
    print(generate_get_function())

if __name__ == "__main__":
    main()
