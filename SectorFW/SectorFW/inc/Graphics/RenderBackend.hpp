#pragma once

#include <vector>
#include "RenderTypes.h"

#include "RenderGraph.hpp"

namespace SectorFW
{
	namespace Graphics
	{
		template<typename Derived, PointerType RTV, PointerType SRV, PointerType Buffer>
		class RenderBackendBase {
		public:
			void AddResourceManagerToRenderService(RenderGraph<Derived, RTV, SRV, Buffer>& graph) {
				static_cast<Derived*>(this)->AddResourceManagerToRenderServiceImpl(graph);
			}

			void SetPrimitiveTopology(PrimitiveTopology topology) {
				static_cast<Derived*>(this)->SetPrimitiveTopologyImpl(topology);
			}

			void SetRasterizerState(RasterizerStateID state) {
				static_cast<Derived*>(this)->SetRasterizerStateImpl(state);
			}

			void SetBlendState(BlendStateID state) {
				static_cast<Derived*>(this)->SetBlendStateImpl(state);
			}

			void SetDepthStencilState(DepthStencilStateID state) {
				static_cast<Derived*>(this)->SetDepthStencilStateImpl(state);
			}

			void SetRenderTargets(const std::vector<RTV>& rtvs, void* dsv) {
				static_cast<Derived*>(this)->SetRenderTargetsImpl(rtvs, dsv);
			}

			void BindSRVs(const std::vector<SRV>& srvs, UINT startSlot = 0) {
				static_cast<Derived*>(this)->BindSRVsImpl(srvs, startSlot);
			}

			void BindCBVs(const std::vector<Buffer>& cbvs, UINT startSlot = 0) {
				static_cast<Derived*>(this)->BindCBVsImpl(cbvs, startSlot);
			}

			void BindGlobalCBVs(const std::vector<BufferHandle>& cbvs) {
				static_cast<Derived*>(this)->BindGlobalCBVsImpl(cbvs);
			}

			void ExecuteDraw(const DrawCommand& cmd, bool usePSORastarizer) {
				static_cast<Derived*>(this)->ExecuteDrawImpl(cmd, usePSORastarizer);
			}

			void ExecuteDrawInstanced(const std::vector<DrawCommand>& cmds, bool usePSORastarizer) {
				static_cast<Derived*>(this)->ExecuteDrawInstancedImpl(cmds, usePSORastarizer);
			}

			void ProcessDeferredDeletes(uint64_t currentFrame) {
				static_cast<Derived*>(this)->ProcessDeferredDeletesImpl(currentFrame);
			}
		};
	}
}