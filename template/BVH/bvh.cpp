#include "precomp.h"

#include "bvh.h"

void calculate_bounds(Box* voxel_objects, uint node_idx, BVHNode* pool, uint* indices)
{
    BVHNode& node = pool[node_idx];
    node.min = float3(1e30f);
    node.max = float3(-1e30f);

    for (uint first = node.left_first, i = 0; i < node.count; i++)
    {
        uint idx = indices[first + i];
        Box& leaf = voxel_objects[idx];
        node.min = fminf(node.min, leaf.min);
        node.max = fmaxf(node.max, leaf.max);
    }
}

void BVH::construct_bvh(Box* voxel_objects)
{
    // Create Index Array
    indices = new uint[N];
    for (int i = 0; i < N; i++)
        indices[i] = i;

    // Allocate BVH Root Node
    pool = new BVHNode[N * 2];
    root = &pool[0];
    pool_ptr = 2;

    // Subdivide Root Node
    root->left_first = 0;
    root->count = N;
    calculate_bounds(voxel_objects, root->left_first, pool, indices);
    root->subdivide(root->left_first, voxel_objects, pool, pool_ptr, indices, nodes_used);
}

void intersect_voxel(Ray& ray, Box& box)
{
    //ray.steps++;

    float3 b[2] = {box.min, box.max};
    // test if the ray intersects the box
    const int signx = ray.D.x < 0, signy = ray.D.y < 0, signz = ray.D.z < 0;
    float tmin = (b[signx].x - ray.O.x) * ray.rD.x;
    float tmax = (b[1 - signx].x - ray.O.x) * ray.rD.x;
    const float tymin = (b[signy].y - ray.O.y) * ray.rD.y;
    const float tymax = (b[1 - signy].y - ray.O.y) * ray.rD.y;
    if (tmin > tymax || tymin > tmax)
        goto miss;
    tmin = max(tmin, tymin), tmax = min(tmax, tymax);
    const float tzmin = (b[signz].z - ray.O.z) * ray.rD.z;
    const float tzmax = (b[1 - signz].z - ray.O.z) * ray.rD.z;
    if (tmin > tzmax || tzmin > tmax)
        goto miss;
    if ((tmin = max(tmin, tzmin)) > 0)
        if (ray.t < tmin)
            goto miss;
        else
        {
            ray.t = tmin;
            return;
        }
miss:
    /*ray.t = 1e34f*/ return;
}

void BVH::intersect_bvh(Box* voxel_objects, Ray& ray, const uint node_idx)
{
    BVHNode* node = &pool[node_idx], *stack[64];
    uint stack_ptr = 0;

    while (1)
    {
        float t = ray.t;
        ray.steps++;

        if (node->is_leaf())
        {
            for (uint i = 0; i < node->count; i++)
                intersect_voxel(ray, voxel_objects[indices[node->left_first + i]]);
            if (stack_ptr == 0)
                break;
            else
                node = stack[--stack_ptr];

            continue;
        }
        BVHNode* child1 = &pool[node->left_first];
        BVHNode* child2 = &pool[node->left_first + 1];
#if AMD_CPU
        float dist1 = intersect_aabb(ray, child1->min, child1->max);
        float dist2 = intersect_aabb(ray, child2->min, child2->max);
#else 
        float dist1 = intersect_aabb_sse(ray, child1->min4, child1->max4);
        float dist2 = intersect_aabb_sse(ray, child2->min4, child2->max4);
#endif
        if (dist1 > dist2)
        {
            swap(dist1, dist2);
            swap(child1, child2);
        }
        if (dist1 == 1e34f)
        {
            if (stack_ptr == 0)
                break;
            else
                node = stack[--stack_ptr];
        }
        else
        {
            node = child1;
            if (dist2 != 1e34f)
                stack[stack_ptr++] = child2;
        }
    }
}
#if AMD_CPU
float BVH::intersect_aabb(const Ray& ray, const float3 bmin, const float3 bmax)
{
    float tx1 = (bmin.x - ray.O.x) * ray.rD.x, tx2 = (bmax.x - ray.O.x) * ray.rD.x;
    float tmin = min(tx1, tx2), tmax = max(tx1, tx2);
    float ty1 = (bmin.y - ray.O.y) * ray.rD.y, ty2 = (bmax.y - ray.O.y) * ray.rD.y;
    tmin = max(tmin, min(ty1, ty2)), tmax = min(tmax, max(ty1, ty2));
    float tz1 = (bmin.z - ray.O.z) * ray.rD.z, tz2 = (bmax.z - ray.O.z) * ray.rD.z;
    tmin = max(tmin, min(tz1, tz2)), tmax = min(tmax, max(tz1, tz2));
    if (tmax >= tmin && tmin < ray.t && tmax > 0)
        return tmin;
    else
        return 1e34f;
}
#else
float BVH::intersect_aabb_sse(const Ray& ray, const __m128 bmin4, const __m128 bmax4)
{
    //static __m128 mask4 = _mm_cmpeq_ps(_mm_setzero_ps(), _mm_set_ps(1, 0, 0, 0));
    /*const __m128i imask4 = _mm_set_epi32(0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);
    const __m128 mask4 = (__m128&)imask4;
    __m128 t1 = _mm_mul_ps(_mm_sub_ps(_mm_and_ps(bmin4, mask4), ray.O4), ray.rD4);
    __m128 t2 = _mm_mul_ps(_mm_sub_ps(_mm_and_ps(bmax4, mask4), ray.O4), ray.rD4);
    __m128 vmax4 = _mm_max_ps(t1, t2), vmin4 = _mm_min_ps(t1, t2);
    float tmax = min(vmax4.m128_f32[0], min(vmax4.m128_f32[1], vmax4.m128_f32[2]));
    float tmin = max(vmin4.m128_f32[0], max(vmin4.m128_f32[1], vmin4.m128_f32[2]));
    if (tmax >= tmin && tmin < ray.t && tmax > 0)
        return tmin;
    else
        return 1e34f;*/

    /* idea to use fmsub to save 1 instruction came from <http://www.joshbarczak.com/blog/?p=787> */
    const __m128 rd = _mm_mul_ps(ray.O4, ray.rD4); /* You can even cache this so you only have to compute it once :) */
    const __m128 t1 = _mm_fmsub_ps(bmin4, ray.rD4, rd);
    const __m128 t2 = _mm_fmsub_ps(bmax4, ray.rD4, rd);
    const __m128 vmax4 = _mm_max_ps(t1, t2), vmin4 = _mm_min_ps(t1, t2);
    const float tmax = min(vmax4.m128_f32[0], min(vmax4.m128_f32[1], vmax4.m128_f32[2]));
    const float tmin = max(vmin4.m128_f32[0], max(vmin4.m128_f32[1], vmin4.m128_f32[2]));
    const bool hit = (tmax > 0 && tmax >= tmin);
    return hit ? tmin : 1e34f;
}
#endif

float evaluate_sah(Box* voxel_objects, uint* indices, BVHNode& node, int axis, float pos)
{
    aabb left_box, right_box;
    int left_count = 0, right_count = 0;
    for (uint i = 0; i < node.count; i++)
    {
        Box& box = voxel_objects[indices[node.left_first + i]];
        if (box.get_center()[axis] < pos)
        {
            left_count++;
            left_box.Grow(box.max);
            left_box.Grow(box.min);
        }
        else
        {
            right_count++;
            right_box.Grow(box.max);
            right_box.Grow(box.min);
        }
    }
    float cost = left_count * left_box.Area() + right_count * right_box.Area();
    return cost > 0 ? cost : 1e34f;
}

void BVHNode::subdivide(uint node_idx, Box* voxel_objects, BVHNode* pool, uint pool_ptr, uint* indices, uint& nodes_used)
{
    BVHNode& node = pool[node_idx];

    // determine split axis using Surface Area Heuristic
    int best_axis = -1;
    float best_pos = 0.0f, best_cost = 1e34f;

    for (int axis = 0; axis < 3; axis++)
    {
        for (uint i = 0; i < node.count; i++)
        {
            Box& box = voxel_objects[indices[node.left_first + i]];
            float candidate_pos = box.get_center()[axis];
            float cost = evaluate_sah(voxel_objects, indices, node, axis, candidate_pos);
            if (cost < best_cost)
                best_pos = candidate_pos, best_axis = axis, best_cost = cost;
        }
    }
    int axis = best_axis;
    float split_pos = best_pos;
    
    float3 e = node.max - node.min;
    float parent_area = e.x * e.y + e.y * e.z + e.z * e.x;
    float parent_cost = node.count * parent_area;

    if (best_cost >= parent_cost)
        return;

    // in-place partition
    int i = node.left_first;
    int j = i + node.count - 1;
    while (i <= j)
    {
        if (voxel_objects[indices[i]].get_center()[axis] < split_pos)
            i++;
        else
            swap(indices[i], indices[j--]);
    }
    // abort split if one of the sides is empty
    int leftCount = i - node.left_first;
    if (leftCount == 0 || leftCount == node.count)
        return;
    // create child nodes
    int leftChildIdx = nodes_used++;
    int rightChildIdx = nodes_used++;
    pool[leftChildIdx].left_first = node.left_first;
    pool[leftChildIdx].count = leftCount;
    pool[rightChildIdx].left_first = i;
    pool[rightChildIdx].count = node.count - leftCount;
    node.left_first = leftChildIdx;
    node.count = 0;
    calculate_bounds(voxel_objects, leftChildIdx, pool, indices);
    calculate_bounds(voxel_objects, rightChildIdx, pool, indices);

    subdivide(leftChildIdx, voxel_objects, pool, pool_ptr, indices, nodes_used);
    subdivide(rightChildIdx, voxel_objects, pool, pool_ptr, indices, nodes_used);
}
