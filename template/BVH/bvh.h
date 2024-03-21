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

#if _M_AMD64 
    float3 min, max;
    uint left_first, count;
#elif _M_IX86
    union {
        struct
        {
            float3 min;
            uint left_first;
        };
        __m128 min4;
    };
    union {
        struct
        {
            float3 max;
            uint count;
        };
        __m128 max4;
    };
#endif
};

class BVH
{
  public:
    void construct_bvh(Box* voxel_objects);

    void intersect_bvh(Box* voxel_objects, Ray& ray, const uint node_idx);
#if _M_AMD64
    float intersect_aabb(const Ray& ray, const float3 bmin, const float3 bmax);
#elif _M_IX86
    float intersect_aabb_sse(const Ray& ray, const __m128 bmin4, const __m128 bmax4);
#endif

    

    

    uint* indices;
    BVHNode* pool;
    BVHNode* root;
    uint pool_ptr;
    uint nodes_used = 1;
};