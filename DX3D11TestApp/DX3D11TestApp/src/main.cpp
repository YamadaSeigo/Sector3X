/*****************************************************************//**
 * @file   main.cpp
 * @brief  メインヘッダー
 * @author seigo_t03b63m
 * @date   May 2025
 *********************************************************************/

#include "Sector11FW/inc/WindowHandler.h"

constexpr const char* WINDOW_TITLE = _T("DX3D11TestApp"); // ウィンドウのタイトル

constexpr uint32_t WINDOW_WIDTH = 720; // ウィンドウの幅
constexpr uint32_t WINDOW_HEIGHT = 540; // ウィンドウの高さ


int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_  HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	using namespace Sector11FW;

	// ウィンドウの作成
	WindowHandler::Create(hInstance, nCmdShow, WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT);

	// メインループ
	WindowHandler::Run([]() {
		// ここにメインループの処理を書く
		// 例: 描画処理、入力処理など
		});

	// ウィンドウの破棄
	auto param = WindowHandler::Destroy();

	return param;
}