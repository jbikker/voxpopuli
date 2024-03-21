#include "precomp.h"
#include <stb_image.h>

static float3 uint_to_rgb(uint value);

float3 Ray::GetNormal() const
{
    // return the voxel normal at the nearest intersection
    const float3 I1 = (O + t * D) * WORLDSIZE; // our scene size is (1,1,1), so this scales each voxel to (1,1,1)
    const float3 fG = fracf(I1);
    const float3 d = min3(fG, 1.0f - fG);
    const float mind = min(min(d.x, d.y), d.z);
    const float3 sign = Dsign * 2 - 1;
    return float3(mind == d.x ? sign.x : 0, mind == d.y ? sign.y : 0, mind == d.z ? sign.z : 0);
    // TODO:
    // - *only* in case the profiler flags this as a bottleneck:
    // - This function might benefit from SIMD.
}

float3 Ray::GetAlbedo(VoxelData* voxel_data) const
{
    // return the (floating point) albedo at the nearest intersection
    return voxel_data[voxel].color;
}

float2 Ray::GetUV() const // Source: Milan
{
    float3 N = GetNormal();
    float3 I = O + t * D;

    float u = 0.0f, v = 0.0f;
    if (N.y != 0)
    {
        float xSteps = I.x * WORLDSIZE;
        float zSteps = I.z * WORLDSIZE;

        u = xSteps - floor(xSteps);
        v = zSteps - floor(zSteps);
    }
    else if (N.z != 0)
    {
        float xSteps = I.x * WORLDSIZE;
        float ySteps = I.y * WORLDSIZE;

        u = xSteps - floor(xSteps);
        v = ySteps - floor(ySteps);
        v = 1 - v;
    }
    else if (N.x != 0)
    {
        float zSteps = I.z * WORLDSIZE;
        float ySteps = I.y * WORLDSIZE;

        u = zSteps - floor(zSteps);
        v = ySteps - floor(ySteps);
        v = 1 - v;
    }

    return float2(u, v);
}

Cube::Cube(const float3 pos, const float3 size)
{
    // set cube bounds
    b[0] = pos;
    b[1] = pos + size;
}

float Cube::Intersect(const Ray& ray) const
{
    // test if the ray intersects the cube
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
        goto miss; // yeah c has 'goto' ;)
    if ((tmin = max(tmin, tzmin)) > 0)
        return tmin;
miss:
    return 1e34f;
}

bool Cube::Contains(const float3& pos) const
{
    // test if pos is inside the cube
    return pos.x >= b[0].x && pos.y >= b[0].y && pos.z >= b[0].z && pos.x <= b[1].x && pos.y <= b[1].y && pos.z <= b[1].z;
}

Scene::Scene()
{
    int x, y, channels;
    uint8_t* texture = stbi_load("assets/blue.png", &x, &y, &channels, 0);

    for (int i = 0; i < 256; i++)
    {
        voxel_data[i].color = uint_to_rgb(RandomUInt());
        voxel_data[i].texture.data = texture;
        voxel_data[i].texture.width = x;
        voxel_data[i].texture.height = y;
        voxel_data[i].texture.channels = channels;
    }

    // the voxel world sits in a 1x1x1 cube
    cube = Cube(float3(0, 0, 0), float3(1, 1, 1));
    // initialize the scene using Perlin noise, parallel over z
    //
    // grid = (uint8_t*)MALLOC64( GRIDSIZE3 * sizeof( uint8_t ) );
    // memset( grid, 0, GRIDSIZE3 * sizeof( uint8_t ) );

    for (size_t i = 0; i < GRIDLAYERS; i++)
    {
        uint8_t b = 1 << i;
        int grid_size = pow((GRIDSIZE / b), 3) * sizeof(uint8_t);
        grids[i] = (uint8_t*)MALLOC64(grid_size);
        memset(grids[i], 0, grid_size);
    }

    //saber = Lightsaber(grids, voxel_data);

    #pragma omp parallel for schedule(dynamic)
    for (int z = 0; z < WORLDSIZE; z++)
    {
        const float fz = (float)z / WORLDSIZE;
        for (int y = 0; y < WORLDSIZE; y++)
        {
            const float fy = (float)y / WORLDSIZE;
            float fx = 0;
            for (int x = 0; x < WORLDSIZE; x++, fx += 1.0f / WORLDSIZE)
            {
                const float n = noise3D(fx, fy, fz);

                if (n > 0.09f)
                {
                    for (size_t i = 0; i < GRIDLAYERS; i++)
                    {
                        uint8_t b = (1 << i);
                        #if !AMD_CPU
                            grids[i][morton_encode(floor(x / b), floor(y / b), floor(z / b))] = 1;
                        #else
                            grids[i][(x / b) + (y / b) * (GRIDSIZE / b) + (z / b) * (GRIDSIZE / b) * (GRIDSIZE / b)] = 1;
                        #endif
                    }
                }
            }
        }
    }
}

static float3 uint_to_rgb(uint value)
{
    uint r = (value >> 16) & 255;
    uint g = (value >> 8) & 255;
    uint b = value & 255;
    return float3(r, g, b) / 256.0f;
}

void Scene::Set(const uint x, const uint y, const uint z, const uint8_t v)
{
    grid[x + y * GRIDSIZE + z * GRIDSIZE2] = v;
}

bool Scene::Setup3DDDA(const Ray& ray, DDAState& state) const
{
    // if ray is not inside the world: advance until it is
    state.t = 0;
    if (!cube.Contains(ray.O))
    {
        state.t = cube.Intersect(ray);
        if (state.t > 1e33f)
            return false; // ray misses voxel data entirely
    }

    // setup amanatides & woo - assume world is 1x1x1, from (0,0,0) to (1,1,1)
    const float inv = 1.0f / state.scale;
    const int gridSize = GRIDSIZE * inv;
    const float cellSize = 1.0f / gridSize;
    state.step = make_int3(1 - ray.Dsign * 2);
    const float3 posInGrid = gridSize * (ray.O + (state.t + 0.00005f) * ray.D);
    const float3 gridPlanes = (ceilf(posInGrid) - ray.Dsign) * cellSize;
    const int3 P = clamp(make_int3(posInGrid), 0, gridSize - 1);
    state.X = P.x, state.Y = P.y, state.Z = P.z;
    state.tdelta = (cellSize * float3(state.step) * ray.rD);
    state.tmax = ((gridPlanes - ray.O) * ray.rD);
    return true;
}

void Scene::FindNearest(Ray& ray, const int layer) const
{
    // setup Amanatides & Woo grid traversal
    DDAState s;
    s.scale = (1 << (layer - 1));
    if (!Setup3DDDA(ray, s))
        return;

    const int grid_size = GRIDSIZE / s.scale;
    while (1)
    {
        ray.steps++;
        #if !AMD_CPU
            const uint cell = grids[layer - 1][morton_encode(s.X, s.Y, s.Z)];
        #else
            const uint cell = grids[layer - 1][s.X + s.Y * grid_size + s.Z * grid_size * grid_size];
        #endif
       
        if (cell)
        {
            ray.t = s.t;
            ray.I = ray.O + ray.t * ray.D;

            if (layer > 1)
            {
                Ray new_ray(ray.I, ray.D);
                FindNearest(new_ray, layer - 1);
                ray.voxel = new_ray.voxel;
                ray.I = new_ray.I;
                ray.t += new_ray.t;
                ray.steps += new_ray.steps;
            }
            else
                ray.voxel = cell;

            break;
        }
        if (s.tmax.x < s.tmax.y){ if (s.tmax.x < s.tmax.z) { s.t = s.tmax.x, s.X += s.step.x; if (s.X >= grid_size) break; s.tmax.x += s.tdelta.x; }
            else { s.t = s.tmax.z, s.Z += s.step.z; if (s.Z >= grid_size) break; s.tmax.z += s.tdelta.z; }}
        else { if (s.tmax.y < s.tmax.z) { s.t = s.tmax.y, s.Y += s.step.y; if (s.Y >= grid_size) break; s.tmax.y += s.tdelta.y; }
            else { s.t = s.tmax.z, s.Z += s.step.z; if (s.Z >= grid_size) break; s.tmax.z += s.tdelta.z;}}
    }
}

bool Scene::IsOccluded(const Ray& ray, const int layer) const
{
    // setup Amanatides & Woo grid traversal
    DDAState s, bs;
    s.scale = (1 << (layer - 1));
    const int grid_size = GRIDSIZE / s.scale;
    if (!Setup3DDDA(ray, s))
        return false;

    // start stepping
    while (s.t < ray.t)
    {
        const uint cell = grids[layer - 1][morton_encode(s.X, s.Y, s.Z)];
        if (cell)
        {
            if (layer != 1)
            {
                #if !AMD_CPU
                    Ray sec_ray(ray.O + s.t * ray.D, ray.D, ray.t - s.t);
                #else
                    Ray sec_ray(ray.O + ray.t * ray.D, ray.D);
                #endif
                return IsOccluded(sec_ray, layer - 1);
            }
            else
            {
                return true;
            }

            break;
        }
        if (s.tmax.x < s.tmax.y)
        {
            if (s.tmax.x < s.tmax.z)
            {
                if ((s.X += s.step.x) >= grid_size)
                    return false;
                s.t = s.tmax.x, s.tmax.x += s.tdelta.x;
            }
            else
            {
                if ((s.Z += s.step.z) >= grid_size)
                    return false;
                s.t = s.tmax.z, s.tmax.z += s.tdelta.z;
            }
        }
        else
        {
            if (s.tmax.y < s.tmax.z)
            {
                if ((s.Y += s.step.y) >= grid_size)
                    return false;
                s.t = s.tmax.y, s.tmax.y += s.tdelta.y;
            }
            else
            {
                if ((s.Z += s.step.z) >= grid_size)
                    return false;
                s.t = s.tmax.z, s.tmax.z += s.tdelta.z;
            }
        }
    }
    return false;
}