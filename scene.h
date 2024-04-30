#pragma once

// high level settings
#define WORLDSIZE 128 // power of 2. Warning: max 512 for a 512x512x512x4 bytes = 512MB world!

// low-level / derived
#define WORLDSIZE2	(WORLDSIZE*WORLDSIZE)
#define GRIDSIZE	WORLDSIZE
#define GRIDSIZE2	(GRIDSIZE*GRIDSIZE)
#define GRIDSIZE3	(GRIDSIZE*GRIDSIZE*GRIDSIZE)

// epsilon
#define EPSILON		0.00001f

namespace Tmpl8 {

class Scene
{
public:
	struct DDAState
	{
		int3 step;
		uint X, Y, Z;
		float t;
		float3 tdelta;
		float3 tmax;
	};
	Scene();
	void FindNearest( Ray& ray ) const;
	bool IsOccluded( Ray& ray ) const;
	void Set( const uint x, const uint y, const uint z, const uint v );
	unsigned int* grid; // voxel payload is 'unsigned int', interpretation of the bits is free!
private:
	bool Setup3DDDA( Ray& ray, DDAState& state ) const;
};

}