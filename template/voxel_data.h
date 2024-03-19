#pragma once

struct VoxelData
{
    float3 color;
    enum class MaterialType
    {
        DIFFUSE,
        REFLECTIVE,
    };
    struct Texture
    {
        uint8_t* data;
        int width, height, channels;
    } texture;
};