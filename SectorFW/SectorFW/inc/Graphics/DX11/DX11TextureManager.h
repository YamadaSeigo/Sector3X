#pragma once

#include "_dx11inc.h"
#include "../RenderTypes.h"
#include "Util/ResouceManagerBase.hpp"

#include <unordered_map>
#include <mutex>
#include <optional>

namespace SectorFW
{
	namespace Graphics
	{
		struct DX11TextureCreateDesc {
			std::string path;
			bool forceSRGB = false;
		};

		struct DX11TextureData {
			ComPtr<ID3D11ShaderResourceView> srv = nullptr;
		private:
			std::string path; // キャッシュ用のパス

			friend class DX11TextureManager;
		};

		class DX11TextureManager : public ResourceManagerBase<
			DX11TextureManager, TextureHandle, DX11TextureCreateDesc, DX11TextureData>
		{
		public:
			explicit DX11TextureManager(ID3D11Device* device);

			std::optional<TextureHandle> FindExisting(const DX11TextureCreateDesc& desc) {
				// path を正規化してから検索するのが吉
				if (auto it = pathToHandle.find(desc.path); it != pathToHandle.end())
					return it->second;
				return std::nullopt;
			}
			void RegisterKey(const DX11TextureCreateDesc& desc, TextureHandle h) {
				pathToHandle.emplace(desc.path, h);
			}

			DX11TextureData CreateResource(const DX11TextureCreateDesc& desc, TextureHandle h);

			void RemoveFromCaches(uint32_t idx);
			void DestroyResource(uint32_t idx, uint64_t /*currentFrame*/);

		private:
			ID3D11Device* device;

			std::unordered_map<std::string, TextureHandle> pathToHandle;
		};
	}
}
