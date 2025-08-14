#ifdef _WIN32

#include <iostream>

#include "WindowHandler.h"

namespace SectorFW
{
	// ウィンドウが作成されたかどうかのフラグ
	bool WindowHandler::m_isCreated = false;
	// ウィンドウハンドル
	HWND WindowHandler::m_hWnd;
	// ウィンドウインスタンスハンドル
	HINSTANCE WindowHandler::m_hInstance;
	// メッセージ構造体
	MSG WindowHandler::m_msg;
	// マウス入力ハンドラ
	Input::WinMouseInput* WindowHandler::m_mouseInput = nullptr;

	void WindowHandler::Run(void(*pLoop)())
	{
		LONG dx = 0, dy = 0;
		while (true)
		{
			// 新たにメッセージがあれば
			if (PeekMessage(&m_msg, NULL, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&m_msg);
				DispatchMessage(&m_msg);

				// 「WM_QUIT」メッセージを受け取ったらループを抜ける
				if (m_msg.message == WM_QUIT) {
					break;
				}

				continue;
			}

			// メッセージが無ければ、ループ処理を実行
			pLoop();

			m_mouseInput->ConsumeDelta(dx, dy);
		}
	}

	LRESULT WindowHandler::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg) {
		case WM_CREATE:
		{
			static Input::WinMouseInput mouseInput(hwnd);
			m_mouseInput = &mouseInput;
			m_mouseInput->RegisterRawInput(false);
			return 0;
		}

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
			if (!m_mouseInput->IsCaptured())
				m_mouseInput->ToggleCapture(true);
			return 0;

		case WM_KEYDOWN:
			if (wParam == VK_ESCAPE && m_mouseInput->IsCaptured()) {
				m_mouseInput->ToggleCapture(false);
				return 0;
			}
			break;

		case WM_INPUT:
			m_mouseInput->HandleRawInput(lParam);
			return 0;
		case WM_KILLFOCUS:
			m_mouseInput->OnFocusLost();
			return 0;

			//case WM_ACTIVATE:
			//	if (LOWORD(wParam) == WA_INACTIVE) {
			//		m_mouseInput->OnFocusLost();  // 安全のため解除
			//	}
			//	else {
			//		m_mouseInput->OnFocus();      // 必要なら再キャプチャ
			//	}
			//	break;

		case WM_MOVE:
		case WM_SIZE:
			m_mouseInput->Reclip();
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		case WM_CLOSE:  // 「x」ボタンが押されたら
		{
			int res = MessageBoxA(NULL, "終了しますか？", "確認", MB_OKCANCEL);
			if (res == IDOK)
			{
				DestroyWindow(m_hWnd);  // 「WM_DESTROY」メッセージを送る
			}
		}
		break;
		default:
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
		}

		return 0;
	}

	void WindowHandler::CreateConsoleWindow()
	{
		// Allocates a new console for the calling process
		AllocConsole();

		// Redirect standard input, output, and error to the console window
		freopen_s((FILE**)stdin, "CONIN$", "r", stdin);
		freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
		freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);

		// Optional: Set console title
		SetConsoleTitle("Debug Console");

		std::cout << "Debugging Console Initialized!" << std::endl;
	}
}

#endif // _WIN32