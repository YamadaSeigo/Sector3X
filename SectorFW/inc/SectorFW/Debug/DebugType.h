/*****************************************************************//**
 * @file   DebugType.h
 * @brief デバッグ用の構造体を定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

namespace SectorFW
{
	namespace Debug {
		/**
		 * @brief ライン描画用の頂点構造体
		 */
		struct LineVertex {
			Math::Vec3f pos;
			uint32_t rgba = 0xFFFFFFFF;
		};
		/**
		 * @brief デバッグ用の頂点構造体（位置、法線、UV）
		 */
		struct VertexPNUV {
			Math::Vec3f pos;
			Math::Vec3f normal;
			Math::Vec2f uv;
		};
	}
}
