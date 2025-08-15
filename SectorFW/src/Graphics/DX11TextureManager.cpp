#include "Graphics/DX11/DX11TextureManager.h"

#ifdef _DEBUG
#include "../../third_party/DirectXTex/MDdx64/include/DirectXTex.h"
#else
#include "../../third_party/DirectXTex/MDx64/include/DirectXTex.h"
#endif //! _DEBUG

namespace SectorFW
{
	namespace Graphics
	{
		bool EndsWith(const std::string& s, const std::string& suffix) {
			if (s.size() < suffix.size()) return false;
			return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
		}

		DX11TextureManager::DX11TextureManager(ID3D11Device* device) : device(device) {}

		DX11TextureData DX11TextureManager::CreateResource(const DX11TextureCreateDesc& desc, TextureHandle h)
		{
			DX11TextureData tex{};

			std::wstring wpath(desc.path.begin(), desc.path.end());

			DirectX::ScratchImage image;
			DirectX::TexMetadata metadata;
			HRESULT hr = E_FAIL;

			if (EndsWith(desc.path, ".dds") || EndsWith(desc.path, ".DDS")) {
				// DDS読み込み
				hr = DirectX::LoadFromDDSFile(wpath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
			}
			else {
				// WIC (PNG, JPGなど)
				hr = DirectX::LoadFromWICFile(wpath.c_str(),
					desc.forceSRGB ? DirectX::WIC_FLAGS_FORCE_SRGB : DirectX::WIC_FLAGS_NONE,
					&metadata, image);
			}
			if (FAILED(hr)) {
				throw std::runtime_error("Failed to load texture: " + desc.path);
			}

			// sRGB強制
			if (desc.forceSRGB && !DirectX::IsSRGB(metadata.format)) {
				metadata.format = DirectX::MakeSRGB(metadata.format);
			}

			// ミップマップがなければ生成
			if (metadata.mipLevels == 1) {
				DirectX::ScratchImage mipChain;
				hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(),
					DirectX::TEX_FILTER_DEFAULT, 0, mipChain);
				if (SUCCEEDED(hr)) {
					image = std::move(mipChain);
					metadata = image.GetMetadata();
				}
			}

			// GPUリソース作成
			ID3D11Resource* texture = nullptr;
			hr = DirectX::CreateTexture(device, image.GetImages(), image.GetImageCount(), metadata, &texture);
			if (FAILED(hr)) {
				throw std::runtime_error("Failed to create GPU texture for: " + desc.path);
			}

			// SRV作成
			ID3D11ShaderResourceView* srv = nullptr;
			hr = device->CreateShaderResourceView(texture, nullptr, &srv);
			if (FAILED(hr)) {
				texture->Release();
				throw std::runtime_error("Failed to create SRV for: " + desc.path);
			}

			texture->Release();

			tex.srv = srv;
			tex.path = desc.path; // キャッシュ用パス

			return tex;
		}

		void DX11TextureManager::RemoveFromCaches(uint32_t idx)
		{
			auto& d = slots[idx].data;
			if (!d.path.empty()) pathToHandle.erase(d.path);
		}

		void DX11TextureManager::DestroyResource(uint32_t idx, uint64_t)
		{
			slots[idx].data.srv.Reset();
		}
	}
}