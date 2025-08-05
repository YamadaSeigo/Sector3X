#pragma once

#include <vector>
#include "RenderTypes.h"

#include "RenderGraph.hpp"

namespace SectorFW
{
	namespace Graphics
	{
		template<typename Derived, PointerType RTV, PointerType SRV>
		class RenderBackendBase {
		public:
			void AddResourceManagerToRenderService(RenderGraph<Derived, RTV, SRV>& graph) {
				static_cast<Derived*>(this)->AddResourceManagerToRenderServiceImpl(graph);
			}

			void SetRenderTargets(const std::vector<RTV>& rtvs, void* dsv) {
				static_cast<Derived*>(this)->SetRenderTargetsImpl(rtvs, dsv);
			}

			void BindSRVs(const std::vector<SRV>& srvs, UINT startSlot = 0) {
				static_cast<Derived*>(this)->BindSRVsImpl(srvs, startSlot);
			}

			void BindCBVs(const std::vector<ID3D11Buffer*>& cbvs, UINT startSlot = 0) {
				static_cast<Derived*>(this)->BindCBVsImpl(cbvs, startSlot);
			}

			void ExecuteDraw(const DrawCommand& cmd) {
				static_cast<Derived*>(this)->ExecuteDrawImpl(cmd);
			}

			void ExecuteDrawInstanced(const std::vector<DrawCommand>& cmds) {
				static_cast<Derived*>(this)->ExecuteDrawInstancedImpl(cmds);
			}

			void ProcessDeferredDeletes(uint64_t currentFrame) {
				static_cast<Derived*>(this)->ProcessDeferredDeletesImpl(currentFrame);
			}
		};
	}
}