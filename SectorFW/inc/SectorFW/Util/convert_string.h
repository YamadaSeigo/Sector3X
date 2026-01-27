#include <string>
#include <cstdint>

namespace SFW
{
	static void AppendUtf8(std::string& out, uint32_t cp)
	{
		if (cp <= 0x7F) {
			out.push_back((char)cp);
		}
		else if (cp <= 0x7FF) {
			out.push_back((char)(0xC0 | (cp >> 6)));
			out.push_back((char)(0x80 | (cp & 0x3F)));
		}
		else if (cp <= 0xFFFF) {
			out.push_back((char)(0xE0 | (cp >> 12)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (cp & 0x3F)));
		}
		else {
			out.push_back((char)(0xF0 | (cp >> 18)));
			out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (cp & 0x3F)));
		}
	}

	static std::string WCharToUtf8_portable(const wchar_t* w)
	{
		if (!w) return {};
		std::string out;
		out.reserve(64);

		// wchar_t が 2byte(UTF-16) か 4byte(UTF-32) かで処理を分ける
		if constexpr (sizeof(wchar_t) == 2) {
			// UTF-16
			for (const uint16_t* p = (const uint16_t*)w; *p; ) {
				uint32_t u = *p++;

				// サロゲートペア
				if (0xD800 <= u && u <= 0xDBFF) {
					uint32_t lo = *p;
					if (0xDC00 <= lo && lo <= 0xDFFF) {
						++p;
						uint32_t cp = 0x10000 + (((u - 0xD800) << 10) | (lo - 0xDC00));
						AppendUtf8(out, cp);
					}
					else {
						// 壊れたサロゲート：U+FFFD
						AppendUtf8(out, 0xFFFD);
					}
				}
				else if (0xDC00 <= u && u <= 0xDFFF) {
					// 単体の下位サロゲート：U+FFFD
					AppendUtf8(out, 0xFFFD);
				}
				else {
					AppendUtf8(out, u);
				}
			}
		}
		else {
			// UTF-32 (一般に Linux/macOS)
			for (const uint32_t* p = (const uint32_t*)w; *p; ++p) {
				uint32_t cp = *p;
				// 不正範囲を軽くケア
				if (cp > 0x10FFFF || (0xD800 <= cp && cp <= 0xDFFF)) cp = 0xFFFD;
				AppendUtf8(out, cp);
			}
		}

		return out;
	}
}
