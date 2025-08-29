#ifdef _WIN32

#include <iostream>

#include "WindowHandler.h"

#include "GUI/ImGuiLayer.h"

#ifdef _ENABLE_IMGUI
#include "../external/imgui/imgui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include "../../external/imgui/imgui_impl_win32.h"
#endif // _D3D11_IMGUI

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
#ifdef _ENABLE_IMGUI
        if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
            return 0; // ImGui が処理したらここで完了
#endif

        switch (uMsg) {
        case WM_CREATE:
        {
            static Input::WinMouseInput mouseInput(hwnd);
            m_mouseInput = &mouseInput;
            m_mouseInput->RegisterRawInput(false);
            return 0; // 自前で完了
        }

        // 左クリックは ImGui に任せるとして、右クリックで“捕捉ON”
        case WM_RBUTTONDOWN:
            if (!m_mouseInput->IsCaptured())
                m_mouseInput->ToggleCapture(true);
            return 0; // 自前で完了

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE && m_mouseInput->IsCaptured()) {
                m_mouseInput->ToggleCapture(false);
                return 0; // 自前で完了
            }
            break; // 既定処理に回す

        case WM_INPUT:
            m_mouseInput->HandleRawInput(lParam);
            return 0; // 自前で完了（RawInputは自分で消費）

        case WM_SETFOCUS:
            // 必要ならここで RawInput の登録を通常モードへ戻すなど
            break; // 既定処理へ

        case WM_KILLFOCUS:
            m_mouseInput->OnFocusLost();
            return 0; // 自前で完了（状態クリア）

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                m_mouseInput->OnFocusLost();
                // ここは「既定処理も必要」なので return せず break
            }
            break; // 既定処理へ

        case WM_MOUSEACTIVATE:
            // 既定処理に回すのが安全（クリックでアクティブ化の挙動）
            break;

        case WM_MOVE:
        case WM_SIZE:
            m_mouseInput->Reclip();
            return 0; // 自前で完了

        case WM_CLOSE:
        {
            int res = MessageBoxA(hwnd, "終了しますか？", "確認", MB_OKCANCEL | MB_ICONQUESTION);
            if (res == IDOK) {
                DestroyWindow(hwnd); // → WM_DESTROY へ
            }
            return 0; // 自前で完了（キャンセル時も既定破棄はさせない）
        }

        case WM_DESTROY:
            m_mouseInput->OnFocusLost(); // 念のため状態クリア
            PostQuitMessage(0);
            return 0; // 自前で完了
        }

        // ここが超重要：処理していないメッセージは既定処理へ
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
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