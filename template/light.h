#pragma once

enum class LightType
{
    POINT,
    DIRECTIONAL,
    SPOT
};

struct Light
{
    Light(LightType _type, float3 _pos = float3(0.0f, 0.0f, 0.0f), float3 _color = float3(0.0f, 0.0f, 0.0f), float3 _dir = float3(0.0f, 0.0f, 0.0f), float _cutoff_angle = 0.0f,
          float _spot_exponent = 0.0f)
        : type(_type), pos(_pos), color(_color), dir(_dir), cutoff_angle(_cutoff_angle), spot_exponent(_spot_exponent)
    {
    }

    LightType type;

    float3 pos;
    float3 color;
    float3 dir;
    float cutoff_angle;
    float spot_exponent;
    /*float inner_angle;
    float outer_angle;*/
};