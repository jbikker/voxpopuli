#pragma once

class Skydome
{
  public:
    Skydome();

    float3 render(const Ray& ray) const;

  private:
    float* pixels = nullptr;
    int bpp = 0, width = 0, height = 0; // BPP - Bytes Per Pixel
};