
struct IN_PosOnly
{
    float4 pos : SV_POSITION;
};

float main(IN_PosOnly input) : SV_Depth
{
    return input.pos.z;
}