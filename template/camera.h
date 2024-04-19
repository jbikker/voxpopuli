#pragma once

// default screen resolution
#define SCRWIDTH	1280
#define SCRHEIGHT	800
// #define FULLSCREEN
// #define DOUBLESIZE

namespace Tmpl8 {

class Camera
{
public:
	Camera();
	~Camera();
	Ray GetPrimaryRay( const int x, const int y );
	bool HandleInput( const float t );
	float aspect = (float)SCRWIDTH / (float)SCRHEIGHT;
	float3 camPos, camTarget;
	float3 topLeft, topRight, bottomLeft;
};

}