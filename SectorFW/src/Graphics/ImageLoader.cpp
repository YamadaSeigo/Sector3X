
#include "Graphics/ImageLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image/stb_image.h"

namespace SFW::Graphics
{
    void StbImageDeleter::operator()(unsigned char* ptr) const noexcept
    {
        if (ptr)
            stbi_image_free(ptr);
    }

	ImageData LoadImageFromFileRGBA8(const std::string& path, bool flipVertically)
    {
        // 読み込み時に上下反転するか
        stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);

        int w = 0, h = 0, comp = 0;

        // STBI_rgb_alpha を指定すると必ず RGBA(4ch) で返ってくる
        unsigned char* data =
            stbi_load(path.c_str(), &w, &h, &comp, STBI_rgb_alpha);

        if (!data)
        {
            // 失敗理由は stbi_failure_reason() で取れる
            const char* reason = stbi_failure_reason();
            throw std::runtime_error(
                std::string("Failed to load image: ") + path +
                " reason: " + (reason ? reason : "unknown"));
        }

        ImageData img;
        img.width = w;
        img.height = h;
        img.channels = comp;          // 元画像の実チャネル数
        img.desiredChannels = 4;      // 今回は RGBA8
        img.pixels.reset(data);       // unique_ptr に所有権を渡す

        return img;
    }

    ImageData LoadImageFromFile(const std::string& path, int desiredChannels, bool flipVertically)
    {
        stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);

        int w = 0, h = 0, comp = 0;
        unsigned char* data =
            stbi_load(path.c_str(), &w, &h, &comp, desiredChannels);

        if (!data)
        {
            const char* reason = stbi_failure_reason();
            throw std::runtime_error(
                std::string("Failed to load image: ") + path +
                " reason: " + (reason ? reason : "unknown"));
        }

        ImageData img;
        img.width = w;
        img.height = h;
        img.channels = comp;             // 元画像のチャネル数
        img.desiredChannels = desiredChannels == 0 ? comp : desiredChannels;
        img.pixels.reset(data);
        return img;
    }
}
