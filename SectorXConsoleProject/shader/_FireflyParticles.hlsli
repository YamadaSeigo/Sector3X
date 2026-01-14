// FireflyParticles.hlsli
struct FireflyParticle
{
    float3 posWS;
    float life; // 0..1 or seconds

    float3 velWS;
    uint volumeSlot; // 安定スロット（CPU側で割り当てたもの）

    float phase; // 点滅位相
    float band01; // 0..1（粒子固有の帯オフセット）
    float addSize; // 加算サイズ
    float pad2;
};


struct FireflyVolumeGPU
{
    float3 centerWS;
    float radius;

    float3 color;
    float intensity;

    float targetCount;
    float speed;
    float noiseScale;
    uint volumeSlot;

    uint nearLightBudget;
    uint seed;
   
    float burstT; // 0..1（1=発動直後、時間で0へ）
    
    float pad0_volume;
};
