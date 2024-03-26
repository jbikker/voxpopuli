#include "precomp.h"

#include "bvh.h"

#define OGT_VOX_IMPLEMENTATION
#include "lib/ogt_vox.h"

void calculate_bounds(Box* voxel_objects, uint node_idx, BVHNode* pool, uint* indices)
{
    BVHNode& node = pool[node_idx];
    node.min = float3(1e30f);
    node.max = float3(-1e30f);

    for (uint first = node.left_first, i = 0; i < node.count; i++)
    {
        uint idx = indices[first + i];
        Box& leaf = voxel_objects[idx];
        //node.min = fminf(node.min, leaf.min);
        //node.max = fmaxf(node.max, leaf.max);
        node.min = fminf(node.min, TransformPosition(leaf.min, leaf.model.matrix()));
        node.max = fmaxf(node.max, TransformPosition(leaf.max, leaf.model.matrix()));
    }
}

void BVH::construct_bvh(Box* voxel_objects)
{
    // Create Index Array
    indices = new uint[/*N * N * N*/ N];
    for (int i = 0; i < N /*N * N * N*/; i++)
        indices[i] = i;

    // Allocate BVH Root Node
    pool = new BVHNode[/*N * N * N*/ N * 2];
    root = &pool[0];
    pool_ptr = 2;

    // Subdivide Root Node
    root->left_first = 0;
    root->count = N /*N * N * N*/;
    calculate_bounds(voxel_objects, root->left_first, pool, indices);
    root->subdivide(root->left_first, voxel_objects, pool, pool_ptr, indices, nodes_used);
}

void BVH::intersect_voxel(Ray& ray, Box& box)
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
        {
            //ray.t = tmin;
            find_nearest(ray, box);
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
    const __m128i imask4 = _mm_set_epi32(0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);
    const __m128 mask4 = (__m128&)imask4;
    __m128 t1 = _mm_mul_ps(_mm_sub_ps(_mm_and_ps(bmin4, mask4), ray.O4), ray.rD4);
    __m128 t2 = _mm_mul_ps(_mm_sub_ps(_mm_and_ps(bmax4, mask4), ray.O4), ray.rD4);
    __m128 vmax4 = _mm_max_ps(t1, t2), vmin4 = _mm_min_ps(t1, t2);
    float tmax = min(vmax4.m128_f32[0], min(vmax4.m128_f32[1], vmax4.m128_f32[2]));
    float tmin = max(vmin4.m128_f32[0], max(vmin4.m128_f32[1], vmin4.m128_f32[2]));
    if (tmax >= tmin && tmin < ray.t && tmax > 0)
        return tmin;
    else
        return 1e34f;

    /* idea to use fmsub to save 1 instruction came from <http://www.joshbarczak.com/blog/?p=787> */
    //const __m128 rd = _mm_mul_ps(ray.O4, ray.rD4); /* You can even cache this so you only have to compute it once :) */
    //const __m128 t1 = _mm_fmsub_ps(bmin4, ray.rD4, rd);
    //const __m128 t2 = _mm_fmsub_ps(bmax4, ray.rD4, rd);
    //const __m128 vmax4 = _mm_max_ps(t1, t2), vmin4 = _mm_min_ps(t1, t2);
    //const float tmax = min(vmax4.m128_f32[0], min(vmax4.m128_f32[1], vmax4.m128_f32[2]));
    //const float tmin = max(vmin4.m128_f32[0], max(vmin4.m128_f32[1], vmin4.m128_f32[2]));
    //const bool hit = (tmax > 0 && tmax >= tmin);
    //return hit ? tmin : 1e34f;
}
#endif

bool BVH::setup_3ddda(const Ray& ray, DDAState& state, Box& box)
{
    // if ray is not inside the world: advance until it is
    state.t = 0;
    if (!box.contains(ray.O))
    {
        state.t = intersect_aabb(ray, box.min, box.max);
        if (state.t > 1e33f)
            return false; // ray misses voxel data entirely
    }

    // setup amanatides & woo - assume world is 1x1x1, from (0,0,0) to (1,1,1)
    //const float cellSize = 1.0f / box.size;
    //state.step = make_int3(1 - ray.Dsign * 2);
    //const float3 posInGrid = box.size * (ray.O + (state.t + 0.00005f) * ray.D);
    //const float3 gridPlanes = (ceilf(posInGrid) - ray.Dsign) * cellSize;
    //const int3 P = clamp(make_int3(posInGrid), 0, box.size - 1);
    //state.X = P.x, state.Y = P.y, state.Z = P.z;
    //state.tdelta = cellSize * float3(state.step) * ray.rD;
    //state.tmax = (gridPlanes - ray.O) * ray.rD;
    //// proceed with traversal
    //return true;

    // expressed in world space
    const float3 voxelMinBounds = box.min;
    const float3 voxelMaxBounds = box.max;

    const float gridsizeFloat = static_cast<float>(box.size);
    const float cellSize = 1.0f / gridsizeFloat;
    state.step = make_int3(1 - ray.Dsign * 2);
    // based on our cube position
    const float3 posInGrid = gridsizeFloat * ((ray.O - voxelMinBounds) + (state.t + 0.00005f) * ray.D) / voxelMaxBounds;
    const float3 gridPlanes = (ceilf(posInGrid) - ray.Dsign) * cellSize;
    const int3 P = clamp(make_int3(posInGrid), 0, box.size - 1);
    state.X = P.x, state.Y = P.y, state.Z = P.z;
    state.tdelta = cellSize * float3(state.step) * ray.rD;
    state.tmax = ((gridPlanes * voxelMaxBounds) - (ray.O - voxelMinBounds)) * ray.rD;
    return true;
}

void BVH::find_nearest(Ray& ray, Box& box)
{
    // Save Initial Ray
    Ray initial_ray = ray;

    mat4 model_mat = box.model.matrix();
    mat4 inv_model_mat = model_mat.Inverted();

    // Transform the Ray
    ray.O = TransformPosition(ray.O, inv_model_mat);
    ray.D = TransformVector(ray.D, inv_model_mat);
    ray.rD = float3(1.0f / ray.D.x, 1.0f / ray.D.y, 1.0f / ray.D.z);
    ray.CalculateDsign();

    // Setup Amanatides & Woo Grid Traversal
	DDAState s;
    if (!setup_3ddda(ray, s, box))
        return;
	// Start Stepping
	while (s.t <= ray.t)
	{
        const uint8_t cell = box.grid[(s.Z * box.size * box.size) + (s.Y * box.size) + s.X];

		if (cell)
		{
			ray.t = s.t;
			ray.voxel = cell;

            // If the Ray Intersected With a Primitive Within The BVH
            // Correct the Normal Based on The Transformation
            ray.N = normalize(TransformVector(ray.N, model_mat));
			break;
		}
		if (s.tmax.x < s.tmax.y)
		{
			if (s.tmax.x < s.tmax.z) { s.t = s.tmax.x, s.X += s.step.x; if (s.X >= box.size) break; s.tmax.x += s.tdelta.x; }
			else { s.t = s.tmax.z, s.Z += s.step.z; if (s.Z >= box.size) break; s.tmax.z += s.tdelta.z; }
		}
		else
		{
			if (s.tmax.y < s.tmax.z) { s.t = s.tmax.y, s.Y += s.step.y; if (s.Y >= box.size) break; s.tmax.y += s.tdelta.y; }
			else { s.t = s.tmax.z, s.Z += s.step.z; if (s.Z >= box.size) break; s.tmax.z += s.tdelta.z; }
		}  
	}
    // Restore the Original Ray's Transform
    ray.O = initial_ray.O;
    ray.D = initial_ray.D;
    ray.rD = initial_ray.rD;
    ray.Dsign = initial_ray.Dsign;
}

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

void Box::populate_grid()
{
    /* Load the model file */
    FILE* file = fopen("assets/lightsaber.vox", "rb");
    uint32_t buffer_size = _filelength(_fileno(file));
    uint8_t* buffer = new uint8_t[buffer_size];
    fread(buffer, buffer_size, 1, file);
    fclose(file);

    /* Parse the model file */
    const ogt_vox_scene* scene = ogt_vox_read_scene(buffer, buffer_size);
    delete[] buffer; /* Cleanup */

    /* Grab the first model in the scene */
    auto model = scene->models[0];
    printf("loaded model of size : %i, %i, %i\n", model->size_x, model->size_y, model->size_z);

    size = model->size_x;
    auto grid_size = model->size_x * model->size_y * model->size_z * sizeof(uint8_t);
    grid = (uint8_t*)MALLOC64(grid_size);
    memset(grid, 0, grid_size);

#pragma omp parallel for schedule(dynamic)
    for (int z = 0; z < model->size_z; z++)
    {
        for (int y = 0; y < model->size_y; y++)
        {
            for (int x = 0; x < model->size_x; x++)
            {
                const uint8_t voxel_index = model->voxel_data[(z * model->size_y * model->size_x) + ((model->size_y - y - 1) * model->size_x) + x];

                if (voxel_index != 0)
                {
                    printf("Voxel Index: %u \n", voxel_index);
                    //voxel_data[voxel_index].color.x = scene->palette.color[voxel_index].r / 255.0f;
                    //voxel_data[voxel_index].color.y = scene->palette.color[voxel_index].g / 255.0f;
                    //voxel_data[voxel_index].color.z = scene->palette.color[voxel_index].b / 255.0f;
                }

#if !AMD_CPU
                grid[morton_encode(floor(y / b), floor(z / b), floor(x / b))] = voxel_index == 0 ? 0 : 1;
#else
                grid[(z * model->size_y * model->size_x) + (y * model->size_x) + x] = voxel_index == 0 ? 0 : 1;
#endif
            }
        }
    }
}
