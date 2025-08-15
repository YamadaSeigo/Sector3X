# generate_key_map.py

from collections import OrderedDict

# 共通Key定義（SDL準拠、一部）
key_definitions = OrderedDict([
    ("Unknown", None),
    ("A", ("SDLK_a", "0x41")),
    ("B", ("SDLK_b", "0x42")),
    ("C", ("SDLK_c", "0x43")),
    ("D", ("SDLK_d", "0x44")),
    ("E", ("SDLK_e", "0x45")),
    ("F", ("SDLK_f", "0x46")),
    ("G", ("SDLK_g", "0x47")),
    ("H", ("SDLK_h", "0x48")),
    ("I", ("SDLK_i", "0x49")),
    ("J", ("SDLK_j", "0x4A")),
    ("K", ("SDLK_k", "0x4B")),
    ("L", ("SDLK_l", "0x4C")),
    ("M", ("SDLK_m", "0x4D")),
    ("N", ("SDLK_n", "0x4E")),
    ("O", ("SDLK_o", "0x4F")),
    ("P", ("SDLK_p", "0x50")),
    ("Q", ("SDLK_q", "0x51")),
    ("R", ("SDLK_r", "0x52")),
    ("S", ("SDLK_s", "0x53")),
    ("T", ("SDLK_t", "0x54")),
    ("U", ("SDLK_u", "0x55")),
    ("V", ("SDLK_v", "0x56")),
    ("W", ("SDLK_w", "0x57")),
    ("X", ("SDLK_x", "0x58")),
    ("Y", ("SDLK_y", "0x59")),
    ("Z", ("SDLK_z", "0x5A")),

    ("Num0", ("SDLK_0", "0x30")),
    ("Num1", ("SDLK_1", "0x31")),
    ("Num2", ("SDLK_2", "0x32")),
    ("Num3", ("SDLK_3", "0x33")),
    ("Num4", ("SDLK_4", "0x34")),
    ("Num5", ("SDLK_5", "0x35")),
    ("Num6", ("SDLK_6", "0x36")),
    ("Num7", ("SDLK_7", "0x37")),
    ("Num8", ("SDLK_8", "0x38")),
    ("Num9", ("SDLK_9", "0x39")),

    ("Escape", ("SDLK_ESCAPE", "VK_ESCAPE")),
    ("Enter", ("SDLK_RETURN", "VK_RETURN")),
    ("Tab", ("SDLK_TAB", "VK_TAB")),
    ("Backspace", ("SDLK_BACKSPACE", "VK_BACK")),
    ("Space", ("SDLK_SPACE", "VK_SPACE")),

    ("Left", ("SDLK_LEFT", "VK_LEFT")),
    ("Right", ("SDLK_RIGHT", "VK_RIGHT")),
    ("Up", ("SDLK_UP", "VK_UP")),
    ("Down", ("SDLK_DOWN", "VK_DOWN")),

    ("LShift", ("SDLK_LSHIFT", "VK_LSHIFT")),
    ("RShift", ("SDLK_RSHIFT", "VK_RSHIFT")),
    ("LCtrl", ("SDLK_LCTRL", "VK_LCONTROL")),
    ("RCtrl", ("SDLK_RCTRL", "VK_RCONTROL")),
    ("LAlt", ("SDLK_LALT", "VK_LMENU")),
    ("RAlt", ("SDLK_RALT", "VK_RMENU")),
])

def generate_enum():
    lines = []
    lines.append("enum class Key : uint16_t {")
    for name in key_definitions.keys():
        lines.append(f"    {name},")
    lines.append("    Count")
    lines.append("};")
    return "\n".join(lines)

def generate_sdl_map():
    lines = []
    lines.append("constexpr std::array<Key, 512> SDLKeyToCommonKey = [] {")
    lines.append("    std::array<Key, 512> map{};")
    for i, (name, sdl_vk) in enumerate(key_definitions.items()):
        if sdl_vk is not None:
            sdl_key, _ = sdl_vk
            lines.append(f"    map[{sdl_key}] = Key::{name};")
    lines.append("    return map;")
    lines.append("}();")
    return "\n".join(lines)

def generate_vk_map():
    lines = []
    lines.append("constexpr std::array<Key, 256> VKToCommonKey = [] {")
    lines.append("    std::array<Key, 256> map{};")
    for i, (name, sdl_vk) in enumerate(key_definitions.items()):
        if sdl_vk is not None:
            _, vk_key = sdl_vk
            lines.append(f"    map[{vk_key}] = Key::{name};")
    lines.append("    return map;")
    lines.append("}();")
    return "\n".join(lines)

def main():
    print("// --- Key Enum ---")
    print(generate_enum())
    print()
    print("// --- SDL Key Mapping ---")
    print(generate_sdl_map())
    print()
    print("// --- Windows VK Key Mapping ---")
    print(generate_vk_map())

if __name__ == "__main__":
    main()
