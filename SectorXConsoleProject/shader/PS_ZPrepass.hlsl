struct VSOutputPos
{
    float4 clip : SV_POSITION;
};

float4 main(VSOutputPos input) : SV_TARGET
{
	return float4(1.0f, 1.0f, 1.0f, 1.0f);
}