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
	PASS_3DMAIN_ZPREPASS,
	PASS_3DMAIN_OPAQUE,
	PASS_3DMAIN_LINE,
	PASS_3DMAIN_MAX
};

enum PassUI : uint16_t {
	PASS_UI_MAIN,
	PASS_UI_MAX
};

