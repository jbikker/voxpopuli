#include "precomp.h"

float3 Ray::GetNormal() const
{
	// return the voxel normal at the nearest intersection
	const float3 I1 = (O + t * D) * WORLDSIZE; // our scene size is (1,1,1), so this scales each voxel to (1,1,1)
	const float3 fG = fracf( I1 );
	const float3 d = min3( fG, 1.0f - fG );
	const float mind = min( min( d.x, d.y ), d.z );
	const float3 sign = Dsign * 2 - 1;
	return float3( mind == d.x ? sign.x : 0, mind == d.y ? sign.y : 0, mind == d.z ? sign.z : 0 );
	// TODO:
	// - *only* in case the profiler flags this as a bottleneck:
	// - This function might benefit from SIMD.
}

float3 Ray::GetAlbedo() const
{
	// return the (floating point) albedo at the nearest intersection
	return float3( 1 ); // TODO: base color on voxel value.
}

Cube::Cube( const float3 pos, const float3 size )
{
	// set cube bounds
	b[0] = pos;
	b[1] = pos + size;
}

float Cube::Intersect( const Ray& ray ) const
{
	// test if the ray intersects the cube
	const int signx = ray.D.x < 0, signy = ray.D.y < 0, signz = ray.D.z < 0;
	float tmin = (b[signx].x - ray.O.x) * ray.rD.x;
	float tmax = (b[1 - signx].x - ray.O.x) * ray.rD.x;
	const float tymin = (b[signy].y - ray.O.y) * ray.rD.y;
	const float tymax = (b[1 - signy].y - ray.O.y) * ray.rD.y;
	if (tmin > tymax || tymin > tmax) goto miss;
	tmin = max( tmin, tymin ), tmax = min( tmax, tymax );
	const float tzmin = (b[signz].z - ray.O.z) * ray.rD.z;
	const float tzmax = (b[1 - signz].z - ray.O.z) * ray.rD.z;
	if (tmin > tzmax || tzmin > tmax) goto miss; // yeah c has 'goto' ;)
	if ((tmin = max( tmin, tzmin )) > 0) return tmin;
miss:
	return 1e34f;
}

bool Cube::Contains( const float3& pos ) const
{
	// test if pos is inside the cube
	return pos.x >= b[0].x && pos.y >= b[0].y && pos.z >= b[0].z &&
		pos.x <= b[1].x && pos.y <= b[1].y && pos.z <= b[1].z;
}

Scene::Scene()
{
	// the voxel world sits in a 1x1x1 cube
	cube = Cube( float3( 0, 0, 0 ), float3( 1, 1, 1 ) );
	// initialize the scene using Perlin noise, parallel over z
	grid = (uint*)MALLOC64( GRIDSIZE3 * sizeof( uint ) );
	memset( grid, 0, GRIDSIZE3 * sizeof( uint ) );
	for (int z = 0; z < WORLDSIZE; z++)
	{
		const float fz = (float)z / WORLDSIZE;
		for (int y = 0; y < WORLDSIZE; y++)
		{
			const float fy = (float)y / WORLDSIZE;
			float fx = 0;
			for (int x = 0; x < WORLDSIZE; x++, fx += 1.0f / WORLDSIZE)
			{
				const float n = noise3D( fx, fy, fz );
				Set( x, y, z, n > 0.09f ? 0xffffff : 0 );
			}
		}
	}
}

void Scene::Set( const uint x, const uint y, const uint z, const uint v )
{
	grid[x + y * GRIDSIZE + z * GRIDSIZE2] = v;
}

bool Scene::Setup3DDDA( const Ray& ray, DDAState& state ) const
{
	// if ray is not inside the world: advance until it is
	state.t = 0;
	if (!cube.Contains( ray.O ))
	{
		state.t = cube.Intersect( ray );
		if (state.t > 1e33f) return false; // ray misses voxel data entirely
	}
	// setup amanatides & woo - assume world is 1x1x1, from (0,0,0) to (1,1,1)
	static const float cellSize = 1.0f / GRIDSIZE;
	state.step = make_int3( 1 - ray.Dsign * 2 );
	const float3 posInGrid = GRIDSIZE * (ray.O + (state.t + 0.00005f) * ray.D);
	const float3 gridPlanes = (ceilf( posInGrid ) - ray.Dsign) * cellSize;
	const int3 P = clamp( make_int3( posInGrid ), 0, GRIDSIZE - 1 );
	state.X = P.x, state.Y = P.y, state.Z = P.z;
	state.tdelta = cellSize * float3( state.step ) * ray.rD;
	state.tmax = (gridPlanes - ray.O) * ray.rD;
	// proceed with traversal
	return true;
}

void Scene::FindNearest( Ray& ray ) const
{
	// setup Amanatides & Woo grid traversal
	DDAState s, bs;
	if (!Setup3DDDA( ray, s )) return;
	// start stepping
	while (1)
	{
		const uint cell = grid[s.X + s.Y * GRIDSIZE + s.Z * GRIDSIZE2];
		if (cell)
		{
			ray.t = s.t;
			ray.voxel = cell;
			break;
		}
		if (s.tmax.x < s.tmax.y)
		{
			if (s.tmax.x < s.tmax.z) { s.t = s.tmax.x, s.X += s.step.x; if (s.X >= GRIDSIZE) break; s.tmax.x += s.tdelta.x; }
			else { s.t = s.tmax.z, s.Z += s.step.z; if (s.Z >= GRIDSIZE) break; s.tmax.z += s.tdelta.z; }
		}
		else
		{
			if (s.tmax.y < s.tmax.z) { s.t = s.tmax.y, s.Y += s.step.y; if (s.Y >= GRIDSIZE) break; s.tmax.y += s.tdelta.y; }
			else { s.t = s.tmax.z, s.Z += s.step.z; if (s.Z >= GRIDSIZE) break; s.tmax.z += s.tdelta.z; }
		}
	}
	// TODO:
	// - A nested grid will let rays skip empty space much faster.
	// - Coherent rays can traverse the grid faster together.
	// - Perhaps s.X / s.Y / s.Z (the integer grid coordinates) can be stored in a single uint?
	// - Loop-unrolling may speed up the while loop.
	// - This code can be ported to GPU.
}

bool Scene::IsOccluded( const Ray& ray ) const
{
	// setup Amanatides & Woo grid traversal
	DDAState s, bs;
	if (!Setup3DDDA( ray, s )) return false;
	// start stepping
	while (s.t < ray.t)
	{
		const uint cell = grid[s.X + s.Y * GRIDSIZE + s.Z * GRIDSIZE2];
		if (cell) /* we hit a solid voxel */ return s.t < ray.t;
		if (s.tmax.x < s.tmax.y)
		{
			if (s.tmax.x < s.tmax.z) { if ((s.X += s.step.x) >= GRIDSIZE) return false; s.t = s.tmax.x, s.tmax.x += s.tdelta.x; }
			else { if ((s.Z += s.step.z) >= GRIDSIZE) return false; s.t = s.tmax.z, s.tmax.z += s.tdelta.z; }
		}
		else
		{
			if (s.tmax.y < s.tmax.z) { if ((s.Y += s.step.y) >= GRIDSIZE) return false; s.t = s.tmax.y, s.tmax.y += s.tdelta.y; }
			else { if ((s.Z += s.step.z) >= GRIDSIZE) return false; s.t = s.tmax.z, s.tmax.z += s.tdelta.z; }
		}
	}
	return false;
}