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

		DX11TextureData DX11TextureManager::CreateResource(const DX11TextureCreateDesc& desc)
		{
			//将来の拡張性のために、複数排他制御
			std::scoped_lock lock(cacheMutex);

			auto it = cache.find(desc.path);
			if (it != cache.end()) {

				DX11TextureData data;
				data.srv = it->second;
				data.path = it->first;
				return data;
			}

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

			auto node = cache.emplace(desc.path, srv);
			tex.srv = srv;
			tex.path = node.first->first;

			return tex;
		}

		void DX11TextureManager::ScheduleDestroy(uint32_t idx, uint64_t deleteFrame)
		{
			slots[idx].alive = false;
			pendingDelete.push_back({ idx, deleteFrame ,});
		}

		void DX11TextureManager::ProcessDeferredDeletes(uint64_t currentFrame)
		{
			auto it = pendingDelete.begin();
			while (it != pendingDelete.end()) {
				if (it->deleteSync <= currentFrame) {
					auto& data = slots[it->index].data;
					if (data.srv) {
						data.srv->Release();
						data.srv = nullptr;
					}

					// キャッシュからも除去
					{
						std::scoped_lock lock(cacheMutex);
						auto cacheIt = cache.find(std::string(data.path));
						if (cacheIt != cache.end()) {
							cache.erase(cacheIt);
						}
					}

					freeList.push_back(it->index);
					it = pendingDelete.erase(it);
				}
				else {
					++it;
				}
			}
		}
	}
}
