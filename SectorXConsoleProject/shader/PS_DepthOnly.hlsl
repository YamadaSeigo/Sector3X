
struct IN_PosOnly
{
    float4 pos : SV_Position;
};

float main(IN_PosOnly input) : SV_Depth
{
    return 0.1f;
}