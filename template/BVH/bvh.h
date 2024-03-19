#pragma once

constexpr int N = 1; // Amount of Voxel Models in the game

struct Box
{
    float3 min, max;

    float3 get_center() const { return (max - min) * 0.5f; }
};

struct BVHNode
{
    void subdivide(uint node_idx, Box* voxel_objects, BVHNode* pool, uint pool_ptr, uint* indices, uint& nodes_used);
    bool is_leaf() const { return count > 0; }

    aabb bounds;
    uint left, first, count;
};

class BVH
{
  public:
    void construct_bvh(Box* voxel_objects);

    bool intersect_bvh(Box* voxel_objects, Ray& ray, const uint node_idx);
    bool intersect_aabb(const Ray& ray, const float3 bmin, const float3 bmax);

    uint* indices;
    BVHNode* pool;
    BVHNode* root;
    uint pool_ptr;
    uint nodes_used = 1;
};