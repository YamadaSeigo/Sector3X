// PointLightService.cpp
#include "Graphics/PointLightService.h"

namespace SFW::Graphics
{
	void PointLightService::PreUpdate(double deltaTime)
	{
		m_frameIndex++;

		auto countSlot = (m_frameIndex + 1) % 2;

		m_showCount[countSlot].store(0, std::memory_order_release);
	}

	void PointLightService::Reserve(uint32_t capacity)
	{
		std::unique_lock lock(m_mtx);
		m_slots.reserve(capacity);
		m_generation.reserve(capacity);
		m_alive.reserve(capacity);
		m_freeList.reserve(capacity / 2);
	}

	PointLightHandle PointLightService::Create(const PointLightDesc& desc)
	{
		std::unique_lock lock(m_mtx);

		uint32_t idx = 0xFFFFFFFFu;

		if (!m_freeList.empty())
		{
			idx = m_freeList.back();
			m_freeList.pop_back();

			// 再利用：世代を進める（古いハンドル無効化）
			++m_generation[idx];

			m_slots[idx].desc = desc;
			m_slots[idx].alive = true;
			m_alive[idx] = 1;
		}
		else
		{
			idx = (uint32_t)m_slots.size();
			m_slots.push_back(Slot{});
			m_generation.push_back(0);
			m_alive.push_back(1);

			m_slots[idx].desc = desc;
			m_slots[idx].alive = true;
		}

		++m_aliveCount;

		return PointLightHandle{ idx, m_generation[idx] };
	}

	void PointLightService::Destroy(PointLightHandle h)
	{
		std::unique_lock lock(m_mtx);
		if (!isValidNoLock(h)) return;

		Slot& s = m_slots[h.index];
		s.alive = false;
		m_alive[h.index] = 0;
		m_freeList.push_back(h.index);

		if (m_aliveCount > 0) --m_aliveCount;
	}

	bool PointLightService::IsValid(PointLightHandle h) const
	{
		std::shared_lock lock(m_mtx);
		return isValidNoLock(h);
	}

	bool PointLightService::isValidNoLock(PointLightHandle h) const
	{
		if (!h.IsValid()) return false;
		if (h.index >= m_slots.size()) return false;
		if (m_generation[h.index] != h.generation) return false;
		if (!m_slots[h.index].alive) return false;
		return true;
	}

	void PointLightService::SetPosition(PointLightHandle h, const Math::Vec3f& posWS)
	{
		std::unique_lock lock(m_mtx);
		if (!isValidNoLock(h)) return;

		Slot& s = m_slots[h.index];
		s.desc.positionWS = posWS;
	}

	void PointLightService::SetParams(PointLightHandle h, const Math::Vec3f& color, float intensity, float range, bool castsShadow)
	{
		std::unique_lock lock(m_mtx);
		if (!isValidNoLock(h)) return;

		Slot& s = m_slots[h.index];
		s.desc.color = color;
		s.desc.intensity = intensity;
		s.desc.range = range;
		s.desc.castsShadow = castsShadow;
	}

	PointLightDesc PointLightService::Get(PointLightHandle h) const
	{
		std::shared_lock lock(m_mtx);
		if (!isValidNoLock(h)) return PointLightDesc{};
		return m_slots[h.index].desc;
	}

	PointLightDesc PointLightService::GetNoLock(PointLightHandle h) const
	{
		if (!isValidNoLock(h)) return PointLightDesc{};
		return m_slots[h.index].desc;
	}

	bool PointLightService::PushShowHandle(PointLightHandle h)
	{
		if (!IsValid(h)) return false;

		// 1フレーム先のスロットを指定
		auto countSlot = (m_frameIndex + 1) % 2;
		auto n = m_showCount[countSlot].fetch_add(1, std::memory_order_acq_rel);
		if (n >= MAX_FRAME_POINTLIGHT) return false;

		m_showIndex[countSlot][n] = h.index;

		return true;
	}

	const GpuPointLight* PointLightService::BuildGpuLights(uint32_t& outCount) noexcept
	{
		auto countSlot = m_frameIndex % 2;
		auto& showIndex = m_showIndex[countSlot];

		auto n = m_showCount[countSlot].load(std::memory_order_acquire);

		uint32_t slot = m_frameIndex % RENDER_BUFFER_COUNT;
		auto& buffer = m_gpuLights[slot];

		for (uint32_t i = 0; i < n; ++i)
		{
			auto idx = showIndex[i];
			const Slot& s = m_slots[idx];
			GpuPointLight& g = buffer[i];
			g.positionWS = s.desc.positionWS;
			g.range = s.desc.range;
			g.color = s.desc.color;
			g.intensity = s.desc.intensity;
			g.invRange = 1.0f / s.desc.range;
			g.flags = s.desc.castsShadow ? 1u : 0u;
		}

		m_lightCount[slot].store(n, std::memory_order_relaxed);

		outCount = n;

		return buffer;
	}
}