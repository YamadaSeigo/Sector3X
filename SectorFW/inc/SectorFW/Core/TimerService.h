#pragma once

#include "ECS/ServiceContext.hpp"

namespace SFW
{
    class TimerService : public ECS::IUpdateService
    {
    public:
        void PreUpdate(double rawDtSeconds) override {
            m_rawDeltaTime = static_cast<float>(rawDtSeconds);          // 実時間 dt（スローモーションなし）

            // スローモーション / ポーズを反映
            float scale = m_isPaused ? 0.0f : m_timeScale;
            m_scaledDeltaTime = m_rawDeltaTime * scale;

            m_unscaledTotalTime += m_rawDeltaTime;  // 実時間の累積
            m_scaledTotalTime += m_scaledDeltaTime; // ゲーム内時間の累積
        }

        // --- getter ---
        // スローモーション反映済み（ゲームロジック用）
        float GetDeltaTime() const { return m_scaledDeltaTime; }

        // スローモーション無視（UIのアニメ、カメラシェイク等用）
        float GetUnscaledDeltaTime() const { return m_rawDeltaTime; }

        float GetTimeScale() const { return m_timeScale; }
        void  SetTimeScale(float s) { m_timeScale = s; }   // 0.1f なら 1/10 スロー
        void  SetPaused(bool p) { m_isPaused = p; }

        float GetGameTime() const { return m_scaledTotalTime; }   // スローモーションを含むゲーム時間
        float GetRealTime() const { return m_unscaledTotalTime; } // 実際に経過した時間

    private:
        float m_rawDeltaTime = 0.0f; // 生 dt
        float m_scaledDeltaTime = 0.0f; // スケール後 dt

        float m_timeScale = 1.0f; // 0.1f = スローモーション
        bool  m_isPaused = false;

        float m_unscaledTotalTime = 0.0f;
        float m_scaledTotalTime = 0.0f;
    public:
        STATIC_SERVICE_TAG
    };
}
