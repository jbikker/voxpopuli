#pragma once

constexpr int N = 4; // Amount of Voxel Models in the game

struct alignas(32) AABB
{
    float3 min = 1e34f;
    float3 max = -1e34f;

    AABB get_aabb() const { return *this; }
    float3 get_center() const { return min + (max - min) * 0.5f; }
    void grow(const float3 p)
    {
        min = fminf(min, p);
        max = fmaxf(max, p);
    }
    void grow(const AABB& aabb)
    {
        if (aabb.min.x != 1e30f)
        {
            this->grow(aabb.min);
            this->grow(aabb.max);
        }
    }
    float area() const
    {
        const float3 e = max - min;
        return e.x * e.x + e.y * e.y + e.z * e.z;
    }
};

struct alignas(32) Box
{
    uint8_t* grid;
    int size;
    Transform model;
    AABB aabb;
    bool contains(const float3& pos) const
    {
        // test if pos is inside the cube
        return pos.x >= aabb.min.x && pos.y >= aabb.min.y && pos.z >= aabb.min.z && pos.x <= aabb.max.x && pos.y <= aabb.max.y && pos.z <= aabb.max.z;
    }
    void populate_grid();
};

struct BVHNode
{
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

  private:
    void intersect_voxel(Ray& ray, Box& box);
#if AMD_CPU
    float intersect_aabb(const Ray& ray, const float3 bmin, const float3 bmax);
#else
    float intersect_aabb_sse(const Ray& ray, const __m128 bmin4, const __m128 bmax4);
#endif

    bool setup_3ddda(const Ray& ray, DDAState& state, Box& box);
    void find_nearest(Ray& ray, Box& box);

    float find_best_split_plane(Box* voxel_objects, BVHNode& node, int& axis, float& pos) const;

    void subdivide(Box* voxel_objects, BVHNode& node, int id);

  private:
    uint* indices = nullptr;
    BVHNode* pool = nullptr;
    BVHNode* root = nullptr;
    uint pool_ptr = 0;
    uint nodes_used = 2;
    
    //Box* voxel_objects = nullptr;
};