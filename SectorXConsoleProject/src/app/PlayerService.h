#pragma once

class PlayerService : public ECS::IUpdateService
{
public:
	//実際のカメラの位置からのオフセット
	static inline const Math::Vec3f CAMERA_OFFSET = { 0.0f,3.0f,0.0f };

	static inline Math::Vec3f GRAVITY = Math::Vec3f(0.0f, -9.81f, 0.0f);
	static inline Math::Vec3f UP = Math::Vec3f(0.0f, 1.0f, 0.0f);

	static inline float MOVE_SPEED = 10.0f;
	static inline float TURN_SPEED = 10.0f;
	static inline float DEFAULT_FOOT_RADIUS = 5.0f;
	static inline float BOOST_POWER = 2.0f;
	static inline float HOVER_POWER = 1.05f;

	struct GrassFootCB
	{
		// 最大何個まで踏んでいる領域を考慮するか
		static const int MAX_FOOT = 4;
		Math::Vec4f gFootPosWRadiusWS[MAX_FOOT] = {}; // ワールド座標 (足元 or カプセル中心付近)
		float  gFootStrength = 2.0f;         // 全体の曲がり強さ
		float gFootHeightRange = 5.0f;		//影響する高さの範囲
		int    gFootCount = 0;            // 有効な足の数
		float pad = {};
	};

	PlayerService(Graphics::DX11::BufferManager* bufferMgr): bufferMgr(bufferMgr)
	{
		Graphics::DX11::BufferCreateDesc cbDesc;
		cbDesc.name = "PlayerFootCB";
		cbDesc.size = sizeof(GrassFootCB);
		cbDesc.initialData = &grassFoot_buf;
		bufferMgr->Add(cbDesc, hGrassFootCB);

		//imguiデバッグ用スライダー登録
		BIND_DEBUG_SLIDER_FLOAT("Player", "MoveSpeed", &MOVE_SPEED, 0.0f, 50.0f, 0.1f);
		BIND_DEBUG_SLIDER_FLOAT("Player", "TurnSpeed", &TURN_SPEED, 0.0f, 20.0f, 0.1f);
		BIND_DEBUG_SLIDER_FLOAT("Player", "FootStrength", &grassFoot_buf.gFootStrength, 0.0f, 10.0f, 0.01f);
		BIND_DEBUG_SLIDER_FLOAT("Player", "HeightRange", &grassFoot_buf.gFootHeightRange, 0.0f, 50.0f, 0.01f);
		BIND_DEBUG_SLIDER_FLOAT("Player", "DefaultFootRadius", &DEFAULT_FOOT_RADIUS, 0.1f, 20.0f, 0.1f);
	}

	void PreUpdate(double deltaTime)
	{
		currentSlot = (currentSlot + 1) % Graphics::RENDER_BUFFER_COUNT;

		uint32_t footNum = bufferSlot.exchange(0, std::memory_order_acq_rel);
		if (footNum > 0 && footNum <= GrassFootCB::MAX_FOOT)
		{
			grassFoot_buf.gFootCount = footNum;

			auto data = bufferMgr->Get(hGrassFootCB);

			Graphics::DX11::BufferUpdateDesc upDesc;
			upDesc.buffer = data.ref().buffer;
			upDesc.data = &grassFoot_buf;
			upDesc.size = sizeof(GrassFootCB);
			upDesc.isDelete = false;
			bufferMgr->UpdateBuffer(upDesc, currentSlot);
		}
	}

	void SetFootData(const Math::Vec3f& posWS, float radius = DEFAULT_FOOT_RADIUS)
	{
		uint32_t slot = bufferSlot.fetch_add(1, std::memory_order_acq_rel);
		if (slot < GrassFootCB::MAX_FOOT)
		{
			grassFoot_buf.gFootPosWRadiusWS[slot].xyz = posWS;
			grassFoot_buf.gFootPosWRadiusWS[slot].w = radius;
		}
	}

	Graphics::BufferHandle GetFootBufferHandle() const noexcept { return hGrassFootCB; }

	void SetPlayerPosition(const Math::Vec3f& pos)
	{
		std::unique_lock<std::shared_mutex> lock(posMutex);
		currentPlayerPos = pos;
	}

	Math::Vec3f GetPlayerPosition()
	{
		std::shared_lock<std::shared_mutex> lock(posMutex);
		return currentPlayerPos;
	}

private:
	Graphics::DX11::BufferManager* bufferMgr = nullptr;

	std::shared_mutex posMutex;
	Math::Vec3f currentPlayerPos = Math::Vec3f(0.0f, 0.0f, 0.0f);

	GrassFootCB grassFoot_buf{};
	Graphics::BufferHandle hGrassFootCB;
	uint32_t currentSlot = 0;
	std::atomic<uint32_t> bufferSlot;

public:
	STATIC_SERVICE_TAG
};
