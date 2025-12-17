/*****************************************************************//**
 * @file   ImGuiLayer.h
 * @brief ImGuiの描画を管理するクラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once

#if _DEBUG
 // デバッグビルド時は ImGui を有効化
#define _ENABLE_IMGUI
#endif // _DEBUG

#include <memory>

#include "ImGuiBackend.h"

namespace SFW
{
	namespace Debug {
		//前方定義
		class IImGuiBackend;
		/**
		 * @brief バックエンドを使用してImGuiの描画を管理するクラス
		 */
		class ImGuiLayer {
		public:
			/**
			 * @brief コンストラクタ
			 * @details UIBus.cppで実装
			 * @param backend
			 * @return
			 */
			explicit ImGuiLayer(std::unique_ptr<IImGuiBackend> backend);
			~ImGuiLayer();
			/**
			 * @brief ウィンドウの型情報を取得する関数
			 * @return const std::type_info&
			 */
			const std::type_info& GetWindowType() const {
				return backend_->GetWindowType();
			}
			/**
			 * @brief デバイスの型情報を取得する関数
			 * @return const std::type_info&
			 */
			const std::type_info& GetDeviceType() const {
				return backend_->GetDeviceType();
			}
			/**
			 * @brief ImGuiの初期化を行う関数
			 * @param info 初期化情報
			 * @return bool 初期化に成功した場合はtrue、失敗した場合はfalse
			 */
			bool Init(const ImGuiInitInfo& info);
			/**
			 * @brief フレームの開始を行う関数。 ImGui::NewFrame() を含む
			 */
			void BeginFrame();
			/**
			 * @brief UIの描画を行う関数.UIBus.cppで実装
			 */
			void DrawUI(float frameSec);
			/**
			 * @brief フレームの終了を行う関数。 ImGui::Render() まで
			 */
			void EndFrame();
			/**
			 * @brief 描画コマンドの発行を行う関数。 ImGui::Render() の後に呼ぶ
			 */
			void Render();

		private:
			std::unique_ptr<IImGuiBackend> backend_;
		};
	}
}
