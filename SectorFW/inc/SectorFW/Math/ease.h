#pragma once

namespace SFW::Math
{
    /*
   * @brief 滑らかなイージング関数
   */
    inline float EaseSmooth(float t) noexcept
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    /*
	* @brief より滑らかなイージング関数
    */
    inline float EaseSmoother(float t) noexcept
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * t * (t * (6.0f * t - 15.0f) + 10.0f);
    }

    /*
    * @brief 二次イージング関数(下にたわむ)
    */
    inline float EaseInQuad(float t) noexcept
    {
        return t * t;
    }

    /*
	* @brief 二次イージング関数(上にたわむ)
    */
    inline float EaseOutQuad(float t) noexcept
    {
        return 1.0f - (1.0f - t) * (1.0f - t);
    }

	/*
	* @brief 二次イージング関数(両端たわむ)
    */
    inline float EaseInOutQuad(float t) noexcept
    {
        return (t < 0.5f)
            ? 2.0f * t * t
            : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
    }

}
