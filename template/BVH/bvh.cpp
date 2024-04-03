#include "precomp.h"

#include "bvh.h"

#define OGT_VOX_IMPLEMENTATION
#include "lib/ogt_vox.h"

void calculate_bounds(BVHNode& node, Box* voxel_objects, uint* indices)
{
    node.min = float3(1e34f);
    node.max = float3(-1e34f);

    for (uint first = node.left_first, i = 0; i < node.count; i++)
    {
        uint idx = indices[first + i];
        Box& leaf = voxel_objects[idx];
        node.min.x = fminf(node.min.x, leaf.aabb.min.x);
        node.min.y = fminf(node.min.y, leaf.aabb.min.y);
        node.min.z = fminf(node.min.z, leaf.aabb.min.z);
        node.max.x = fmaxf(node.max.x, leaf.aabb.max.x);
        node.max.y = fmaxf(node.max.y, leaf.aabb.max.y);
        node.max.z = fmaxf(node.max.z, leaf.aabb.max.z);
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
    calculate_bounds(*root, voxel_objects, indices);
    subdivide(voxel_objects, *root, 0);
}

void BVH::intersect_voxel(Ray& ray, Box& box)
{
    float tmin = 0.0f, tmax = 1e34f;
    float3 corners[2] = {box.aabb.min, box.aabb.max};

    for (int d = 0; d < 3; ++d)
    {
        bool sign = ray.Dsign[d];
        float bmin = corners[sign][d];
        float bmax = corners[!sign][d];

        float dmin = (bmin - ray.O[d]) * ray.rD[d];
        float dmax = (bmax - ray.O[d]) * ray.rD[d];

        tmin = std::max(dmin, tmin);
        tmax = std::min(dmax, tmax);
        /* Early out check, saves a lot of compute */
        if (tmax < tmin)
            return;
    }

    find_nearest(ray, box);
    //ray.t = tmin;
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
        state.t = intersect_aabb(ray, box.aabb.min, box.aabb.max);
        if (state.t > 1e33f)
            return false; // ray misses voxel data entirely
    }
    // setup amanatides & woo - assume world is 1x1x1, from (0,0,0) to (1,1,1)
    const float3 voxelMinBounds = box.aabb.min;
    const float3 voxelMaxBounds = box.aabb.max - box.aabb.min;
    static const float cellSize = 1.0f / box.size;
    state.step = make_int3(1 - ray.Dsign * 2);
    const float3 posInGrid = box.size * ((ray.O - voxelMinBounds) + (state.t + 0.00005f) * ray.D) / voxelMaxBounds;
    const float3 gridPlanes = (ceilf(posInGrid) - ray.Dsign) * cellSize;
    const int3 P = clamp(make_int3(posInGrid), 0, box.size - 1);
    state.X = P.x, state.Y = P.y, state.Z = P.z;
    state.tdelta = cellSize * float3(state.step) * ray.rD;
    state.tmax = ((gridPlanes * voxelMaxBounds) - (ray.O - voxelMinBounds)) * ray.rD;
    // proceed with traversal
    return true;
}

void BVH::find_nearest(Ray& ray, Box& box)
{
    // Save Initial Ray
    Ray initial_ray = ray;

    mat4 model_mat = box.model.matrix();
    mat4 inv_model_mat = model_mat.Inverted();

    // Setup Amanatides & Woo Grid Traversal
	DDAState s;
    if (!setup_3ddda(ray, s, box))
    {
        return;
    }

    // Transform the Ray
    ray.O = TransformPosition(ray.O, inv_model_mat);
    ray.D = TransformVector(ray.D, inv_model_mat);
    ray.rD = float3(1.0f / ray.D.x, 1.0f / ray.D.y, 1.0f / ray.D.z);
    ray.CalculateDsign();

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
    // Restore the original ray's transform
    ray.O = initial_ray.O;
    ray.D = initial_ray.D;
    ray.rD = initial_ray.rD;
    ray.Dsign = initial_ray.Dsign;
}

float evaluate_sah(Box* voxel_objects, BVHNode& node, int axis, float pos)
{
    AABB l_aabb, r_aabb;
    int l_cnt = 0, r_cnt = 0;
    for (uint i = 0; i < node.count; ++i)
    {
        const Box& box = voxel_objects[node.left_first + i];
        const AABB aabb = box.aabb.get_aabb();
        if (aabb.get_center()[axis] < pos)
        {
            l_cnt++;
            l_aabb.grow(aabb.min);
            l_aabb.grow(aabb.max);
        }
        else
        {
            r_cnt++;
            r_aabb.grow(aabb.min);
            r_aabb.grow(aabb.max);
        }
    }
    float cost = l_cnt * l_aabb.area() + r_cnt * r_aabb.area();
    return cost > 0.0f ? cost : 1e34f;
}

#define SAH_FULL_SWEEP 1
#define SAH_BINS 0
struct Bin
{
    AABB aabb;
    uint count = 0;
};

float BVH::find_best_split_plane(Box* voxel_objects, BVHNode& node, int& axis, float& pos) const
{
    float best_cost = 1e34f;
#if SAH_FULL_SWEEP
    for (int a = 0; a < 3; ++a)
    {
        for (uint i = 0; i < node.count; ++i)
        {
            const Box& box = voxel_objects[node.left_first + i];
            float candidate_pos = box.aabb.get_center()[a];
            float cost = evaluate_sah(voxel_objects, node, a, candidate_pos);
            if (cost < best_cost)
                pos = candidate_pos, axis = a, best_cost = cost;
        }
    }
#elif SAH_BINS
    constexpr int BINS = 8;
    for (int a = 0; a < 3; ++a)
    {
        /* Get the min and max of all primitives in the node */
        float bmin = 1e34f, bmax = -1e34f;
        for (uint i = 0; i < node.count; ++i)
        {
            float3 prim_center = voxel_objects[node.left_first + i].aabb.get_center();
            bmin = fminf(bmin, prim_center[a]);
            bmax = fmaxf(bmax, prim_center[a]);
        }
        if (bmin == bmax)
            continue;

        /* Populate the bins */
        Bin bins[BINS];
        float scale = BINS / (bmax - bmin);
        for (uint i = 0; i < node.count; ++i)
        {
            const Box& prim = voxel_objects[node.left_first + i];
            int bin_idx = fminf(BINS - 1, static_cast<int>((prim.aabb.get_center()[a] - bmin) * scale));
            bins[bin_idx].count++;
            const AABB prim_bounds = prim.aabb.get_aabb();
            bins[bin_idx].aabb.grow(prim_bounds.min);
            bins[bin_idx].aabb.grow(prim_bounds.max);
        }

        /* Gather data for the planes between the bins */
        float l_areas[BINS - 1], r_areas[BINS - 1];
        int l_counts[BINS - 1], r_counts[BINS - 1];
        AABB l_aabb, r_aabb;
        int l_sum = 0, r_sum = 0;
        for (int i = 0; i < BINS - 1; ++i)
        {
            /* Left-side */
            l_sum += bins[i].count;
            l_counts[i] = l_sum;
            l_aabb.grow(bins[i].aabb);
            l_areas[i] = l_aabb.area();
            /* Right-side */
            r_sum += bins[BINS - 1 - i].count;
            r_counts[BINS - 2 - i] = r_sum;
            r_aabb.grow(bins[BINS - 1 - i].aabb);
            r_areas[BINS - 2 - i] = r_aabb.area();
        }

        /* Calculate the SAH cost function for all planes */
        scale = (bmax - bmin) / BINS;
        for (int i = 0; i < BINS - 1; ++i)
        {
            float plane_cost = l_counts[i] * l_areas[i] + r_counts[i] * r_areas[i];
            if (plane_cost < best_cost)
            {
                axis = a;
                pos = bmin + scale * (i + 1);
                best_cost = plane_cost;
            }
        }
    }
#else
    /* 8 candidates results in a decent tree */
    constexpr uint UNIFORM_CANDIDATES = 8;
    for (int a = 0; a < 3; ++a)
    {
        /* Get the min and max of all primitives in the node */
        float bmin = 1e34f, bmax = -1e34f;
        for (uint i = 0; i < node.count; ++i)
        {
            float3 prim_center = voxel_objects[node.left_first + i].aabb.get_center();
            bmin = min(bmin, prim_center[a]);
            bmax = max(bmax, prim_center[a]);
        }
        if (bmin == bmax)
            continue;

        /* Evaluate the SAH of the uniform candidates */
        float scale = (bmax - bmin) / UNIFORM_CANDIDATES;
        for (uint i = 1; i < UNIFORM_CANDIDATES; ++i)
        {
            float candidatePos = bmin + i * scale;
            float cost = evaluate_sah(voxel_objects, node, a, candidatePos);
            if (cost < best_cost)
                pos = candidatePos, axis = a, best_cost = cost;
        }
    }
#endif
    return best_cost;
}

void BVH::subdivide(Box* voxel_objects, BVHNode& node, int id)
{
    /*if (node.count <= 2u)
        return;*/

    /* Determine split based on SAH */
    int split_axis = -1;
    float split_t = 0;
    const float split_cost = find_best_split_plane(voxel_objects, node, split_axis, split_t);

    /* Calculate parent node cost */
    const float3 e = node.max - node.min;
    const float parent_area = e.x * e.x + e.y * e.y + e.z * e.z;
    const float parent_cost = node.count * parent_area;
    if (split_cost >= parent_cost)
        return; /* Split would not be worth it */

    /* Determine which primitives lie on which side */
    int i = node.left_first;
    int j = i + node.count - 1;
    while (i <= j)
    {
        if (voxel_objects[i].aabb.get_center()[split_axis] < split_t)
        {
            i++;
        }
        else
        {
            std::swap(voxel_objects[i], voxel_objects[j--]);
        }
    }

    const int left_count = i - node.left_first;
    if (left_count == 0 || left_count == node.count)
        return;

    /* Initialize child nodes */
    int left_child_idx = nodes_used++;
    int right_child_idx = nodes_used++;
    pool[left_child_idx].left_first = node.left_first;
    pool[left_child_idx].count = left_count;
    pool[right_child_idx].left_first = i;
    pool[right_child_idx].count = node.count - left_count;
    node.left_first = left_child_idx;
    node.count = 0;

    calculate_bounds(pool[left_child_idx], voxel_objects, indices);
    calculate_bounds(pool[right_child_idx], voxel_objects, indices);

    /* Continue subdiving */
    subdivide(voxel_objects, pool[left_child_idx], id + 1);
    subdivide(voxel_objects, pool[right_child_idx], id + 1);
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
