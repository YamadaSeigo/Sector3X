// ImageLoader.hpp
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

	ImageData LoadImageFromFileRGBA8(
		const std::string& path,
		bool flipVertically = false);

	ImageData LoadImageFromFile(
		const std::string& path,
		int desiredChannels = 0,   // 0 なら元のチャネル数のまま
		bool flipVertically = false);
}
