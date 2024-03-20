#pragma once

constexpr int N = 512; // Amount of Voxel Models in the game

struct alignas(32) Box
{
    float3 min, max;

    float3 get_center() const { return max - (max - min) * 0.5f; }
};

struct BVHNode
{
    void subdivide(uint node_idx, Box* voxel_objects, BVHNode* pool, uint pool_ptr, uint* indices, uint& nodes_used);
    bool is_leaf() const { return count > 0; }

    float3 min, max;
    uint left_first, count;
};

class BVH
{
  public:
    void construct_bvh(Box* voxel_objects);

    void intersect_bvh(Box* voxel_objects, Ray& ray, const uint node_idx);
    float intersect_aabb(const Ray& ray, const float3 bmin, const float3 bmax);

    uint* indices;
    BVHNode* pool;
    BVHNode* root;
    uint pool_ptr;
    uint nodes_used = 1;
};