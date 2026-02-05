
#ifndef TILE_DEFERRED
#define TILE_DEFERRED

#define TILE_SIZE 16

#define MAX_LIGHTS_PER_TILE 128

// -----------------------------
// Light type encoding
// -----------------------------
#define LIGHT_TYPE_NORMAL 0u
#define LIGHT_TYPE_FIREFLY 1u

uint EncodeLightIndex(uint type, uint index)
{
    return (type << 31) | (index & 0x7FFFFFFFu);
}
uint DecodeType(uint encoded)
{
    return encoded >> 31;
}
uint DecodeIndex(uint encoded)
{
    return encoded & 0x7FFFFFFFu;
}


#endif // TILEDEFERRED