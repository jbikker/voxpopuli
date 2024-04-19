#pragma once

namespace Tmpl8 {

class Ray
{
public:
	Ray( const float3 origin, const float3 direction, const float rayLength = 1e34f, const uint rgb = 0 );
	float3 IntersectionPoint() const { return O + t * D; }
	float3 GetNormal() const;
	float3 GetAlbedo() const;
	float GetReflectivity( const float3& I ) const; // TODO: implement
	float GetRefractivity( const float3& I ) const; // TODO: implement
	float3 GetAbsorption( const float3& I ) const; // TODO: implement
	// ray data
	float3 O;					// ray origin
	float3 rD;					// reciprocal ray direction
	float3 D;					// ray direction
	float t;					// ray length
	float3 Dsign;				// inverted ray direction signs, -1 or 1
	uint voxel;					// payload of the intersected voxel
	uint axis = 0;				// axis of last plane passed by the ray
	bool inside = false;		// if true, ray started in voxel and t is at exit point
private:
	// min3 is used in normal reconstruction.
	__inline static float3 min3( const float3& a, const float3& b )
	{
		return float3( min( a.x, b.x ), min( a.y, b.y ), min( a.z, b.z ) );
	}
};

};