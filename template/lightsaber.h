#pragma once

struct VoxelData;

class Lightsaber
{
  public:
    Lightsaber() = default;
    Lightsaber(uint8_t** grids, VoxelData* voxel_data);
};