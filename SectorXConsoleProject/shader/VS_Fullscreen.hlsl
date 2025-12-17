struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
    // 3頂点で画面を覆う（-1..1）
    float2 p;
    p.x = (vid == 2) ? 3.0 : -1.0;
    p.y = (vid == 0) ? 3.0 : -1.0;

    VSOut o;
    o.pos = float4(p, 0.0, 1.0);

    // uv は 0..1 に変換（p が -1..3 になる点がミソ）
    o.uv.x = p.x * 0.5 + 0.5;
    o.uv.y = -p.y * 0.5 + 0.5;
    return o;
}
