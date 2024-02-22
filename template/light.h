#pragma once

enum class LightType
{
    POINT,
    DIRECTIONAL,
    SPOT
};

struct Light
{
    Light(LightType _type, float3 _pos = float3(0.0f, 0.0f, 0.0f), float3 _color = float3(0.0f, 0.0f, 0.0f), float3 _dir = float3(0.0f, 0.0f, 0.0f), float _radius = 0.0f) : type(_type), pos(_pos), color(_color), dir(_dir), radius(_radius) {}
    
    void simulate(float time, Ray& primary_ray, Scene& scene);

    LightType type;

    float3 pos;
    float3 color;
    float3 dir;
    float radius;
};