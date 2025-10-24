#include "Core/GameEngine.h"

#ifdef _WIN32 //timeBeginPeriod || timeEndPeriod

#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

#endif //_WIN32

#include "message.h"
#include "SIMD/simd_api.h"

namespace SFW
{
	void InitializeGameEngine(bool initialize)
	{
		// グラフィックデバイスが初期化されていない場合は、エラーを出す
		DYNAMIC_ASSERT_MESSAGE(initialize, "GraphicsDevice is not Configure");

		SIMD::SimdInit(); // SIMD 初期化

#ifdef _WIN32
		timeBeginPeriod(1);  // 精度を1msに設定
#endif //_WIN32
	}

	void UnInitializeGameEngine()
	{
#ifdef _WIN32
		timeEndPeriod(1);    // 後始末
#endif //_WIN32
	}
}