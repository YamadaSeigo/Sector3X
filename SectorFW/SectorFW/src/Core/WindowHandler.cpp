#ifdef _WIN32

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

	void WindowHandler::Run(void(*pLoop)())
	{
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
		}
	}

	LRESULT WindowHandler::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg) {
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