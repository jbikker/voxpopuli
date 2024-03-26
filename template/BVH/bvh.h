#pragma once

constexpr int N = 4; // Amount of Voxel Models in the game

struct alignas(32) Box
{
    float3 min = 0.0f, max = 1.0f;
    uint8_t* grid;
    int size;
    Transform model;

    float3 get_center() const { return (max + min) * 0.5f; }
    bool contains(const float3& pos) const
    {
        // test if pos is inside the cube
        return pos.x >= min.x && pos.y >= min.y && pos.z >= min.z && pos.x <= max.x && pos.y <= max.y && pos.z <= max.z;
    }
    void populate_grid();
};

struct BVHNode
{
    void subdivide(uint node_idx, Box* voxel_objects, BVHNode* pool, uint pool_ptr, uint* indices, uint& nodes_used);
    bool is_leaf() const { return count > 0; }

#if AMD_CPU
    float3 min, max;
    uint left_first, count;
#else
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
    void intersect_voxel(Ray& ray, Box& box);
#if AMD_CPU
    float intersect_aabb(const Ray& ray, const float3 bmin, const float3 bmax);
#else
    float intersect_aabb_sse(const Ray& ray, const __m128 bmin4, const __m128 bmax4);
#endif

    bool setup_3ddda(const Ray& ray, DDAState& state, Box& box);
    void find_nearest(Ray& ray, Box& box);

    uint* indices;
    BVHNode* pool;
    BVHNode* root;
    uint pool_ptr;
    uint nodes_used = 1;
};