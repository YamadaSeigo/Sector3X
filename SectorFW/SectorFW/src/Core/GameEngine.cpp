#include "Core/GameEngine.h"
#include "message.h"

namespace SectorFW
{
	GameEngine::GameEngine(uint64_t fps, std::unique_ptr<IGraphicsDevice>&& graphicsDevice)
		: m_fpsControl(fps), m_graphicsDevice(std::move(graphicsDevice))
	{
		// グラフィックデバイスが初期化されていない場合は、エラーを出す
		DYNAMIC_ASSERT_MESSAGE(m_graphicsDevice->IsInitialized(), "GraphicsDevice is not Configure");
	}

	void GameEngine::MainLoop()
	{
		uint64_t delta_time = 0;

		// 前回実行されてからの経過時間を計測
		delta_time = m_fpsControl.CalcDelta();

		// 更新処理、描画処理を呼び出す
		Update(delta_time);
		Draw(delta_time);

		// 規定時間までWAIT
		m_fpsControl.Wait();
	}

	void GameEngine::Update(uint64_t delta_time)
	{
	}

	void GameEngine::Draw(uint64_t delta_time)
	{
		// 画面をクリア
		float clearColor[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
		m_graphicsDevice->Clear(clearColor);

		// 描画処理
		m_graphicsDevice->Present();
	}
}