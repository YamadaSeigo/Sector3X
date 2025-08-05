#pragma once

#include "_dx11inc.h"
#include "../ResouceManagerBase.hpp"

#include <unordered_map>
#include <mutex>

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
			std::string_view path; // キャッシュ用のパス

			friend class DX11TextureManager;
		};

		class DX11TextureManager : public ResourceManagerBase<
			DX11TextureManager, TextureHandle, DX11TextureCreateDesc, DX11TextureData>
		{
		public:
			explicit DX11TextureManager(ID3D11Device* device);

			DX11TextureData CreateResource(const DX11TextureCreateDesc& desc);

			void ScheduleDestroy(uint32_t idx, uint64_t deleteFrame);
			void ProcessDeferredDeletes(uint64_t currentFrame);

		private:
			ID3D11Device* device;

			std::unordered_map<std::string, ID3D11ShaderResourceView*> cache;
			std::mutex cacheMutex;

			struct PendingDelete {
				uint32_t index;
				uint64_t deleteSync;
			};
			std::vector<PendingDelete> pendingDelete;
		};
	}
}
