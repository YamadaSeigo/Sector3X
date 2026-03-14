/*****************************************************************//**
 * \file   ImageLoader.h
 * \brief STB Image をラップして画像を読み込むユーティリティ
 * \author lenov
 * \date   March 2026
 *********************************************************************/

#pragma once
#include <memory>
#include <string>
#include <stdexcept>

namespace SFW::Graphics
{
	struct StbImageDeleter
	{
		void operator()(unsigned char* ptr) const noexcept;
	};

	struct ImageData
	{
		int width = 0;
		int height = 0;
		int channels = 0;  // 元のチャネル数（3,4 など）
		int desiredChannels = 0; // 変換後のチャネル数（例: 4=RGBA）

		std::unique_ptr<unsigned char, StbImageDeleter> pixels;

		bool IsValid() const noexcept { return pixels != nullptr; }
	};

	/*
	* @brief 画像ファイルを読み込み、RGBA8形式で返します。
	* @param path ファイルのパス
	* @param flipVertically 読み込み時に上下反転するかどうか
	*/
	[[nodiscard]] ImageData LoadImageFromFileRGBA8(
		const std::string& path,
		bool flipVertically = false);

	/*
	* @brief 画像ファイルを読み込みます。
	* @param path ファイルのパス
	* @param desiredChannels 変換後のチャネル数。0 の場合は元のチャネル数のまま。
	* @param flipVertically 読み込み時に上下反転するかどうか
	*/
	[[nodiscard]] ImageData LoadImageFromFile(
		const std::string& path,
		int desiredChannels = 0,   // 0 なら元のチャネル数のまま
		bool flipVertically = false);
}
