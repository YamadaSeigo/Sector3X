/*****************************************************************//**
 * @file   WindowHandler.h
 * @brief  ウィンドウを管理する
 * @author seigo_t03b63m
 * @date   May 2025
 *********************************************************************/

#pragma once

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN    // Windows ヘッダーからほとんど使用されていない部分を除外する
#define NOMINMAX

#include <windows.h>
#include <tchar.h>
#include <cassert>

#include "Input/WinInput.h"

#include "Util/NonCopyable.h"

namespace SectorFW
{
	/**
	 * @brief ウィンドウハンドラ
	 * @class WindowHandler
	 * @details ウィンドウ管理クラス(マルチウィンドウ非対応)
	 */
	class WindowHandler final : NonCopyable
	{
	public:
		/**
		 * @brief コンストラクタ
		 * @details ウィンドウの作成も同時に行う
		 */
		WindowHandler() = delete;
		/**
		 * @brief デストラクタ
		 * @details ウィンドウの破棄
		 */
		~WindowHandler() = default;

#ifdef _CONSOLE
		/**
		 * @brief ウィンドウの作成(コンソールアプリケーション)
		 */
#ifdef _UNICODE
		static void Create(const TCHAR* windowTitle, uint32_t width, uint32_t height)
#else  // !_UNICODE
		static void Create(const char* windowTitle, uint32_t width, uint32_t height)
#endif // !_UNICODE
		{
			// ウィンドウが作成されていたら、何もしない
			if (m_isCreated) return;

			// ウィンドウが作成されたフラグを立てる
			m_isCreated = true;

#ifdef _DEBUG
			//メモリリーク検知
			_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif // _DEBUG

			// ウィンドウクラスの設定
			WNDCLASSEX wc = {
				sizeof(WNDCLASSEX),
				CS_CLASSDC,
				WindowProc,
				0L,
				0L,
				GetModuleHandle(NULL),
				NULL,
				NULL,
				NULL,
				NULL,
				_T("MAIN_WINDOW"),
				NULL };

			RegisterClassEx(&wc);

			m_hInstance = wc.hInstance; // インスタンスハンドルを保存

			// ウィンドウの作成
			m_hWnd = CreateWindow(
				wc.lpszClassName,
				windowTitle,
				WS_OVERLAPPEDWINDOW,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				width,
				height,
				NULL,
				NULL,
				wc.hInstance,
				NULL);

			ShowWindow(m_hWnd, SW_SHOWDEFAULT);
			UpdateWindow(m_hWnd);

			return;
		}

#else // !_CONSOLE
		/**
		 * @brief ウィンドウの作成(ウィンドウアプリケーション)
		 * @param hInstance インスタンスハンドル
		 * @param nCmdShow 表示状態
		 */
#ifdef _UNICODE
		static void Create(HINSTANCE hInstance, int nCmdShow, const TCHAR* windowTitle, uint32_t width, uint32_t height)
#else
		static void Create(HINSTANCE hInstance, int nCmdShow, const char* windowTitle, uint32_t width, uint32_t height)
#endif // _UNICODE
		{
			// ウィンドウが作成されていたら、何もしない
			if (m_isCreated) return;

			// ウィンドウが作成されたフラグを立てる
			m_isCreated = true;

#ifdef _DEBUG
			//コンソール画面起動
			CreateConsoleWindow();

			//メモリリーク検知
			_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif // _DEBUG

			// ウィンドウクラス情報をまとめる
			WNDCLASSEX wc;
			wc.cbSize = sizeof(WNDCLASSEX);
			wc.style = CS_CLASSDC;
			wc.lpfnWndProc = WindowProc;
			wc.cbClsExtra = 0;
			wc.cbWndExtra = 0;
			wc.hInstance = hInstance;
			wc.hIcon = NULL;
			wc.hCursor = LoadCursor(NULL, IDC_ARROW);
			wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
			wc.lpszMenuName = NULL;
			wc.lpszClassName = _T("MAIN_WINDOW");
			wc.hIconSm = NULL;

			RegisterClassEx(&wc);

			m_hInstance = hInstance; // インスタンスハンドルを保存

			// ウィンドウの情報をまとめる
			m_hWnd = CreateWindowEx(
				0, // Extended styles,// 拡張ウィンドウスタイル
				_T("MAIN_WINDOW"),							// ウィンドウクラスの名前
				windowTitle,							// ウィンドウの名前
				WS_OVERLAPPEDWINDOW,					// ウィンドウスタイル
				CW_USEDEFAULT,							// ウィンドウの左上Ｘ座標
				CW_USEDEFAULT,							// ウィンドウの左上Ｙ座標
				width,									// ウィンドウの幅
				height,									// ウィンドウの高さ
				NULL,									// 親ウィンドウのハンドル
				NULL,									// メニューハンドルまたは子ウィンドウID
				hInstance,								// インスタンスハンドル
				NULL);									// ウィンドウ作成データ

			//ウィンドウのサイズを修正
			RECT rc1, rc2;
			GetWindowRect(m_hWnd, &rc1);
			GetClientRect(m_hWnd, &rc2);
			int sx = width;
			int sy = height;
			sx += ((rc1.right - rc1.left) - (rc2.right - rc2.left));
			sy += ((rc1.bottom - rc1.top) - (rc2.bottom - rc2.top));
			SetWindowPos(m_hWnd, NULL, 0, 0, sx, sy, (SWP_NOZORDER |
				SWP_NOOWNERZORDER | SWP_NOMOVE));

			// 指定されたウィンドウの表示状態を設定(ウィンドウを表示)
			ShowWindow(m_hWnd, nCmdShow);
			// ウィンドウの状態を直ちに反映(ウィンドウのクライアント領域を更新)
			UpdateWindow(m_hWnd);

			return;
		}

#endif // !_CONSOLE
		/**
		 * @brief ウィンドウのメッセージループ
		 * @param pLoop ループ処理関数ポインタ
		 * @details メッセージが無ければ、ループ処理を実行
		 */
		static void Run(void(*pLoop)());

		/**
		 * @brief ウィンドウの破棄
		 * @details ウィンドウの破棄
		 */
		static int Destroy()
		{
			// ウィンドウが作成されていなければ、何もしない
			if (!m_isCreated) return 0;

			if (m_mouseInput)
				m_mouseInput->Cleanup();

#ifndef CONSOLE_TRUE
#ifdef _DEBUG
			//コンソール画面閉じる
			FreeConsole();
#endif

#endif //!CONSOLE_TRUE

			UnregisterClass(_T("MAIN_WINDOW"), m_hInstance);

			return (int)m_msg.wParam;
		}

		static HWND GetMainHandle() {
			assert(m_isCreated && "not create window");

			return m_hWnd;
		}

		static Input::WinMouseInput* GetMouseInput() {
			assert(m_isCreated && "not create window");
			return m_mouseInput;
		}

	private:
		/**
		 * @brief ウィンドウプロシージャ
		 * @param hwnd ウィンドウハンドル
		 * @param uMsg メッセージID
		 * @param wParam WPARAM
		 * @param lParam LPARAM
		 * @return LRESULT メッセージの結果
		 */
		static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

		/**
		 * @brief コンソールウィンドウの作成
		 * @details コンソールウィンドウを作成する
		 */
		static void CreateConsoleWindow();
	private:
		// ウィンドウが作成されたかどうかのフラグ
		static bool m_isCreated;
		// ウィンドウハンドル
		static HWND m_hWnd;
		// インスタンスハンドル
		static HINSTANCE m_hInstance;
		// メッセージ構造体
		static MSG m_msg;
		// マウス入力ハンドラ
		static Input::WinMouseInput* m_mouseInput; // マウス入力ハンドラ
	};
}

#endif // _WIN32
