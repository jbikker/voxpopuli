#include "precomp.h"

#include "skydome.h"

#include "stb_image.h"

// Source: https://jacco.ompf2.com/2022/05/27/how-to-build-a-bvh-part-8-whitted-style/

Skydome::Skydome()
{
    // Load Skydome From File
    pixels = stbi_loadf("assets/sky_19.hdr", &width, &height, &bpp, 0); // Skydome Source: https://hdri-haven.com/hdri/rock-formations
    for (int i = 0; i < width * height * 3; i++)
        pixels[i] = sqrtf(pixels[i]); // Gamma Adjustment for Reduced HDR Range
}

float3 Skydome::render(const Ray& ray) const
{
    if (ray.voxel == 0)
    {
        // Sample Sky
        uint u = (uint)width * atan2f(ray.D.z, ray.D.x) * INV2PI - 0.5f;
        uint v = (uint)height * acosf(ray.D.y) * INVPI - 0.5f;
        uint skyIdx = (u + v * width) % (width * height);
        return 0.65f * float3(pixels[skyIdx * 3], pixels[skyIdx * 3 + 1], pixels[skyIdx * 3 + 2]);
    }
}