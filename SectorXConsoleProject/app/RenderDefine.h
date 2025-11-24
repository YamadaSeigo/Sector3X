#pragma once


enum PassGroup {
	GROUP_3D_MAIN,
	GROUP_UI,
	GROUP_MAX
};

constexpr const char* PassGroupName[GROUP_MAX] = {
	"3DMain",
	"UI"
};

enum Pass3DMain : uint16_t {
	PASS_3DMAIN_CASCADE0 =		1u << 0,
	PASS_3DMAIN_CASCADE1 =		1u << 1,
	PASS_3DMAIN_CASCADE2 =		1u << 2,
	PASS_3DMAIN_CASCADE3 =		1u << 3,
	PASS_3DMAIN_ZPREPASS =		1u << 4,
	PASS_3DMAIN_OPAQUE =		1u << 5,
	
};

enum PassUI : uint16_t {
	PASS_UI_3DLINE =			1u << 0,
	PASS_UI_MAIN =				1u << 1,
	PASS_UI_LINE =				1u << 2,
};

