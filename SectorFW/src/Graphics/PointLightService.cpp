// PointLightService.cpp
#include "Graphics/PointLightService.h"

namespace SFW::Graphics
{
    void PointLightService::Reserve(uint32_t capacity)
    {
        std::unique_lock lock(m_mtx);
        m_slots.reserve(capacity);
        m_generation.reserve(capacity);
        m_alive.reserve(capacity);
        m_freeList.reserve(capacity / 2);
        m_dirtyIndices.reserve(capacity / 4);
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
            m_slots[idx].dirty = Dirty_All;
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
            m_slots[idx].dirty = Dirty_All;
            m_slots[idx].alive = true;
        }

        ++m_aliveCount;

        // dirty indexに追加（重複は許容。最適化するならbitset等にする）
        m_dirtyIndices.push_back(idx);

        return PointLightHandle{ idx, m_generation[idx] };
    }

    void PointLightService::Destroy(PointLightHandle h)
    {
        std::unique_lock lock(m_mtx);
        if (!isValidNoLock(h)) return;

        Slot& s = m_slots[h.index];
        s.alive = false;
        m_alive[h.index] = 0;
        s.dirty = Dirty_All; // DestroyもGPU反映が必要ならdirty扱い（無効化フラグ等）
        m_freeList.push_back(h.index);

        if (m_aliveCount > 0) --m_aliveCount;

        m_dirtyIndices.push_back(h.index);
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
        s.dirty |= Dirty_Pos;
        m_dirtyIndices.push_back(h.index);
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
        s.dirty |= Dirty_Params;
        m_dirtyIndices.push_back(h.index);
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

    void PointLightService::BuildGpuLights(std::vector<GpuPointLight>& out) const
    {
        std::shared_lock lock(m_mtx);

        out.clear();
        out.reserve(m_aliveCount);

        for (uint32_t i = 0; i < (uint32_t)m_slots.size(); ++i)
        {
            const Slot& s = m_slots[i];
            if (!s.alive) continue;

            GpuPointLight g{};
            g.positionWS = s.desc.positionWS;
            g.range = s.desc.range;
            g.color = s.desc.color;
            g.intensity = s.desc.intensity;
            g.flags = (s.desc.castsShadow ? 1u : 0u);

            out.push_back(g);
        }
    }

    void PointLightService::CollectDirtyIndices(std::vector<uint32_t>& outDirtyIndices)
    {
        std::unique_lock lock(m_mtx);

        outDirtyIndices = m_dirtyIndices;

        // 重複削除（必要なら）
        std::sort(outDirtyIndices.begin(), outDirtyIndices.end());
        outDirtyIndices.erase(std::unique(outDirtyIndices.begin(), outDirtyIndices.end()), outDirtyIndices.end());
    }

    void PointLightService::ClearDirty()
    {
        std::unique_lock lock(m_mtx);

        for (uint32_t i = 0; i < (uint32_t)m_slots.size(); ++i)
            m_slots[i].dirty = Dirty_None;

        m_dirtyIndices.clear();
    }

    void PointLightService::CollectShadowCandidatesNear(
        const Math::Vec3f& cameraPosWS,
        uint32_t maxCount,
        std::vector<PointLightHandle>& out) const
    {
        std::shared_lock lock(m_mtx);

        struct Cand { uint32_t idx; float d2; };
        std::vector<Cand> cands;
        cands.reserve(64);

        for (uint32_t i = 0; i < (uint32_t)m_slots.size(); ++i)
        {
            const Slot& s = m_slots[i];
            if (!s.alive) continue;
            if (!s.desc.castsShadow) continue;

            cands.push_back(Cand{ i, Math::LengthSquared(s.desc.positionWS, cameraPosWS) });
        }

        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d2 < b.d2; });

        out.clear();
        const uint32_t take = std::min<uint32_t>((uint32_t)cands.size(), maxCount);
        out.reserve(take);

        for (uint32_t k = 0; k < take; ++k)
        {
            const uint32_t idx = cands[k].idx;
            out.push_back(PointLightHandle{ idx, m_generation[idx] });
        }
    }
}
