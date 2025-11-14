#pragma once


enum PassGroup {
	GROUP_3D_MAIN,
	GROUP_SHADOW,
	GROUP_UI,
	GROUP_MAX
};

constexpr const char* PassGroupName[GROUP_MAX] = {
	"3DMain",
	"Shadow",
	"UI"
};

enum Pass3DMain : uint16_t {
	PASS_3DMAIN_ZPREPASS = 1u << 0,
	PASS_3DMAIN_OPAQUE = 1u << 1,
	PASS_3DMAIN_LINE = 1u << 2,
};

enum PassUI : uint16_t {
	PASS_UI_MAIN = 1u << 0,
};

