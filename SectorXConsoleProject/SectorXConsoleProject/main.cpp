
#include "SectorFW/inc/sector11fw.h"
#include "SectorFW/inc/WindowHandler.h"
#include "SectorFW/inc/DX11Graphics.h"


#define WINDOW_NAME "SectorX Console Project"

constexpr uint32_t WINDOW_WIDTH = 720;	// ウィンドウの幅
constexpr uint32_t WINDOW_HEIGHT = 540;	// ウィンドウの高さ

constexpr uint64_t FPS_LIMIT = 60;	// フレームレート制限

int main(void)
{
	using namespace SectorFW;

	// ウィンドウの作成
	WindowHandler::Create(_T(WINDOW_NAME), WINDOW_WIDTH, WINDOW_HEIGHT);

	std::unique_ptr<IGraphicsDevice> graphics = std::make_unique<DX11GraphicsDevice>();
	graphics->Configure(WindowHandler::GetMainHandle(), WINDOW_WIDTH, WINDOW_HEIGHT);

	static GameEngine gameEngine(FPS_LIMIT, std::move(graphics));

	// メッセージループ
	WindowHandler::Run([]() {
		// ここにメインループの処理を書く

		gameEngine.MainLoop();

		});

	return WindowHandler::Destroy();
}