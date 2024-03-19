#include "precomp.h"

#include "bvh.h"

aabb calculate_bounds(Box* voxel_objects, uint node_idx, BVHNode* pool, uint* indices)
{
    BVHNode& node = pool[node_idx];
    node.bounds.bmin3 = float3(1e30f);
    node.bounds.bmax3 = float3(-1e30f);

    for (uint first = node.first, i = 0; i < node.count; i++)
    {
        uint idx = indices[first + i];
        Box& leaf = voxel_objects[idx];
        node.bounds.bmin3 = fminf(node.bounds.bmin3, leaf.min.x);
        node.bounds.bmin3 = fminf(node.bounds.bmin3, leaf.min.y);
        node.bounds.bmin3 = fminf(node.bounds.bmin3, leaf.min.z);
        node.bounds.bmax3 = fmaxf(node.bounds.bmax3, leaf.max.x);
        node.bounds.bmax3 = fmaxf(node.bounds.bmax3, leaf.max.y);
        node.bounds.bmax3 = fmaxf(node.bounds.bmax3, leaf.max.z);
    }
}

void BVH::construct_bvh(Box* voxel_objects)
{
    // Create Index Array
    indices = new uint[N];
    for (int i = 0; i < N; i++)
        indices[i] = i;

    // Allocate BVH Root Node
    pool = new BVHNode[N * 2 - 1];
    root = &pool[0];
    pool_ptr = 2;

    // Subdivide Root Node
    root->first = 0;
    root->count = N;
    root->bounds = calculate_bounds(voxel_objects, root->first, pool, indices);
    root->subdivide(root->first, voxel_objects, pool, pool_ptr, indices, nodes_used);
}

bool intersect_voxel(const Ray& ray, Box& box)
{
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
            return true;
miss:
    return false;
}

bool BVH::intersect_bvh(Box* voxel_objects, Ray& ray, const uint node_idx)
{
    BVHNode& node = pool[node_idx];
    if (!intersect_aabb(ray, node.bounds.bmin3, node.bounds.bmax3))
        return false;
    if (node.is_leaf())
        for (uint i = 0; i < node.count; i++)
            if (intersect_voxel(ray, voxel_objects[indices[node.first + i]]))
                return true;
    else
    {
        if (intersect_bvh(voxel_objects, ray, node.left))
            return true;
        if (intersect_bvh(voxel_objects, ray, node.left + 1))
            return true;
    }
    return false;
}

bool BVH::intersect_aabb(const Ray& ray, const float3 bmin, const float3 bmax)
{
    float tx1 = (bmin.x - ray.O.x) / ray.D.x, tx2 = (bmax.x - ray.O.x) / ray.D.x;
    float tmin = min(tx1, tx2), tmax = max(tx1, tx2);
    float ty1 = (bmin.y - ray.O.y) / ray.D.y, ty2 = (bmax.y - ray.O.y) / ray.D.y;
    tmin = max(tmin, min(ty1, ty2)), tmax = min(tmax, max(ty1, ty2));
    float tz1 = (bmin.z - ray.O.z) / ray.D.z, tz2 = (bmax.z - ray.O.z) / ray.D.z;
    tmin = max(tmin, min(tz1, tz2)), tmax = min(tmax, max(tz1, tz2));
    return tmax >= tmin && tmin < ray.t && tmax > 0;
}

void BVHNode::subdivide(uint node_idx, Box* voxel_objects, BVHNode* pool, uint pool_ptr, uint* indices, uint& nodes_used)
{
    BVHNode& node = pool[node_idx];
    if (node.count < 3)
        return;

    // determine split axis and position
    float3 extent = node.bounds.bmax3 - node.bounds.bmin3;
    int axis = 0;
    if (extent.y > extent.x)
        axis = 1;
    if (extent.z > extent[axis])
        axis = 2;
    float splitPos = node.bounds.bmin3[axis] + extent[axis] * 0.5f;
    // in-place partition
    int i = node.first;
    int j = i + node.count - 1;
    while (i <= j)
    {
        if (voxel_objects[indices[i]].get_center()[axis] < splitPos)
            i++;
        else
            swap(indices[i], indices[j--]);
    }
    // abort split if one of the sides is empty
    int leftCount = i - node.first;
    if (leftCount == 0 || leftCount == node.count)
        return;
    // create child nodes
    int leftChildIdx = nodes_used++;
    int rightChildIdx = nodes_used++;
    pool[leftChildIdx].first = node.first;
    pool[leftChildIdx].count = leftCount;
    pool[rightChildIdx].first = i;
    pool[rightChildIdx].count = node.count - leftCount;
    node.left = leftChildIdx;
    node.count = 0;
    calculate_bounds(voxel_objects, leftChildIdx, pool, indices);
    calculate_bounds(voxel_objects, rightChildIdx, pool, indices);

    subdivide(leftChildIdx, voxel_objects, pool, pool_ptr, indices, nodes_used);
    subdivide(rightChildIdx, voxel_objects, pool, pool_ptr, indices, nodes_used);
}
