/*****************************************************************//**
 * \file   AudioType.h
 * \brief オーディオ管理サービスで使用される基本的な型定義を提供します。サウンドハンドル、ボイスID、再生パラメータなどの構造体が含まれます。
 * \author seigo
 * \date   December 2026
 *********************************************************************/

#pragma once

namespace SFW::Audio
{
	// サウンドを識別するためのハンドル。内部的にはIDを保持し、0は無効なハンドルを表す。
	struct SoundHandle
	{
		uint32_t id = 0;
		explicit operator bool() const noexcept { return id != 0; }
	};

	// SoLoudのボイスハンドルを表す型。0は無効なボイスIDを表します。
	using VoiceID = uint64_t; // store SoLoud::handle; 0 = invalid

	// オーディオ再生のチケットID。再生要求を発行するときに割り当てられ、後でボイスIDに解決される可能性があります。
	struct AudioTicketID
	{
		uint32_t index = UINT32_MAX;
		uint32_t generation = 0;

		// チケットが有効かどうかを確認するためのユーティリティ関数
		bool IsValid() const noexcept { return index != UINT32_MAX; }
		// 無効なチケットIDを返すための静的関数
		static constexpr AudioTicketID Invalid() noexcept { return AudioTicketID{ UINT32_MAX, 0 }; }

		bool operator==(const AudioTicketID& o) const noexcept {
			return index == o.index && generation == o.generation;
		}
	};

	// サウンド再生のパラメータをまとめた構造体。音量、パン、ピッチ、ループ設定などを含みます。
	struct AudioPlayParams
	{
		float volume = 1.0f; // 0=無音、1=デフォルト、2=2倍など
		float pan = 0.0f; // -1=左、0=中央、1=右
		float pitch = 1.0f; // 1=デフォルト、0.5=半分、2=2倍など
		bool  loop = false; // ループ再生するかどうか
		bool  paused = false; // 再生開始時に一時停止するかどうか

		// 3D optional
		bool  is3D = false;
		Math::Vec3f pos;
		Math::Vec3f vec;
	};
}
