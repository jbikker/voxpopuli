#include "precomp.h"

// -----------------------------------------------------------
// Initialize the renderer
// -----------------------------------------------------------
void Renderer::Init()
{
    frames = 0;

    // create fp32 rgb pixel buffer to render to
    accumulator = (float4*)MALLOC64(SCRWIDTH * SCRHEIGHT * 16);
    memset(accumulator, 0, SCRWIDTH * SCRHEIGHT * 16);
    // try to load a camera
    FILE* f = fopen("camera.bin", "rb");
    if (f)
    {
        fread(&camera, 1, sizeof(Camera), f);
        fclose(f);
    }

    voxel_objects = new Box[N];
    for (int i = 0; i < N; i++)
    {
        voxel_objects[i].min = float3(RandomFloat(), RandomFloat(), RandomFloat()) * i;
        voxel_objects[i].max = voxel_objects[i].min + 1.0f;
    }

    bvh.construct_bvh(voxel_objects);

    skydome = Skydome();
}

bool activate_lightsaber = false;

float roughness = 0.3f;
float3 point_a = 0.0f;
float3 point_b = 0.0f;
float rad = 0.005f;

float capsule_intersect(Ray& ray, float radius)
{
    // Capsule Intersection (Source: https://iqules/intersectors/ilezles.org/artic)
    float3 ba = point_b - point_a; // BA vector
    float3 oa = ray.O - point_a;
    float baba = dot(ba, ba);
    float bard = dot(ba, normalize(ray.D));
    float baoa = dot(ba, oa);
    float rdoa = dot(normalize(ray.D), oa);
    float oaoa = dot(oa, oa);
    float a = baba - bard * bard;
    float b = baba * rdoa - baoa * bard;
    float c = baba * oaoa - baoa * baoa - radius * radius * baba;
    float h = b * b - a * c;

    if (h >= 0.0f) 
    {
        float t = (-b - sqrt(h)) / a;
        float t2 = (-b + sqrt(h)) / a;

        float y = baoa + t * bard;
        // Body
        if (y > 0.0f && y < baba)
        {
            return (t + t2) * 0.5f;
        }
        // Caps
        float3 oc = (y <= 0.0f) ? oa : ray.O - point_b;
        b = dot(normalize(ray.D), oc);
        c = dot(oc, oc) - radius * radius;
        h = b * b - c;
        if (h > 0.0f)
        {
            return ((-b - sqrt(h)) + (-b + sqrt(h))) * 0.5f;
        }
    }
    return -1.0f;
}

float3 light_color = float3(1.0f, 0.0f, 0.0f);

// -----------------------------------------------------------
// Evaluate light transport
// -----------------------------------------------------------
float3 Renderer::Trace(Ray& ray)
{
    float outer_rad = rad * 5.0f;
    float3 lightsaber_cont = 0.0f;
    float intensity = 0.0f;
    bool lightsaber_hit = false;
    float lightsaber_t = 1e34f;

    if (activate_lightsaber)
    {
        float core_t = capsule_intersect(ray, rad);
        float outer_t = capsule_intersect(ray, outer_rad);

        float3 min, max;
        if (point_a.y > point_b.y)
        {
            min = point_b;
            max = point_a;
        }
        else
        {
            min = point_a;
            max = point_b;
        }

        if (core_t >= 0.0f)
        {
            //return float3(1.0f, 1.0f, 1.0f);
            lightsaber_t = core_t * 0.5f;
            lightsaber_cont = 1.0f;
            intensity = 1.0f;
        }
        else if (outer_t >= 0.0f)
        {
            lightsaber_hit = true;

            lightsaber_t = outer_t * 0.5f;

            float3 p = ray.O + outer_t * normalize(ray.D);

            float y_diff = max.y - min.y;
            float y_change = p.y - min.y;
            float final_t = y_change / y_diff;

            float3 center = lerp(min, max, final_t);

            float fact = std::max(1.0f - (length(p - center) / outer_rad), 0.0f);

            lightsaber_cont = light_color * fact;

                intensity = lightsaber_cont.x;
            if (lightsaber_cont.y > intensity)
                intensity = lightsaber_cont.y;
            if (lightsaber_cont.z > intensity)
                intensity = lightsaber_cont.z;
        }
    }

    ray.t = 0.0f;
    scene.FindNearest(ray, GRIDLAYERS);

    if (grid_view)
        return ray.steps / 64.0f;

    if (ray.voxel == 0)
        return skydome.render(ray) * (1.0f - intensity) + lightsaber_cont;

    float3 I = ray.O + (ray.t - 0.00001f) * ray.D;
    const float3 L = normalize(float3(1, 4, 0.5f));
    float3 N = ray.GetNormal();
    float3 albedo = /*ray.GetAlbedo(scene.voxel_data)*/ float3(1.0f);
    
    float3 final_color = /*albedo;*/ 0.0f;

    /*if (ray.voxel == 220)
    {
        point_a = ray.O + ray.t * ray.D;
        point_a += float3(0.02f, 0.1f, 0.0f);
    }
    point_b = point_a + float3(0.0f, 0.9f, 0.0f);*/

    //// Convert u, v to texture coordinates
    //VoxelData::Texture tex = scene.voxel_data[ray.voxel].texture;
    //float2 uv = ray.GetUV();
    //int texelX = uv.x * (tex.width);
    //int texelY = uv.y * (tex.height);

    //// Calculate index into texture data
    //int index = (texelY * tex.width + texelX) * tex.channels;

    //// Extract color channels from texture data
    //uint8_t r = tex.data[index];
    //uint8_t g = tex.data[index + 1];
    //uint8_t b = tex.data[index + 2];

    //albedo = float3(r, g, b) / 255.0f;

    for (size_t i = 0; i < lights.size(); i++)
    {
        switch (lights[i].type)
        {
        case LightType::POINT:
            {
                float3 s_ray_dir = normalize(lights[i].pos - I);
                float angle = dot(N, s_ray_dir);

                if (angle <= 0)
                    continue;
                
                float dist = length(lights[i].pos - I);
                float falloff = max(1 / (dist * dist) - 0.25f, 0.0f);

                if (falloff <= 0.0f)
                    continue;

                //Ray shadow_ray = Ray(I, s_ray_dir);
                Ray shadow_ray = Ray(lights[i].pos, -s_ray_dir, dist);
                if (scene.IsOccluded(shadow_ray, GRIDLAYERS))
                    continue;
                final_color += albedo * lights[i].color * falloff * angle;
            }
            break;
        case LightType::DIRECTIONAL: 
            {
                float angle = dot(N, normalize(-lights[i].dir));

                if (angle <= 0)
                    continue;

                Ray shadow_ray = Ray(I * lights[i].dir * 1000.0f, -lights[i].dir);
                if (scene.IsOccluded(shadow_ray, GRIDLAYERS))
                    continue;
                final_color += albedo * lights[i].color * angle;
            }
            break;
        case LightType::SPOT: 
            {
                // Source: https://math.hws.edu/graphicsbook/c7/s2.html#webgl3d.2.6
                float spot_factor = 1.0f;

                float3 spot_dir = normalize(lights[i].dir);
                float3 s_ray_dir = normalize(lights[i].pos - I);

                float a = dot(N, s_ray_dir);
                if (a <= 0)
                    continue;

                if (lights[i].cutoff_angle <= 0.0f)
                    continue;

                float angle = dot(spot_dir, s_ray_dir);

                if (angle >= lights[i].cutoff_angle)    
                    spot_factor = powf(angle, lights[i].spot_exponent); 
                else
                    spot_factor = 0.0f;

                Ray shadow_ray = Ray(lights[i].pos, -s_ray_dir, length(lights[i].pos - I));
                if (scene.IsOccluded(shadow_ray, GRIDLAYERS))
                    continue;

                final_color += albedo * lights[i].color * spot_factor * angle * a;
                
                 //float3 spot_dir = lights[i].dir;
                 //float3 s_ray_dir = lights[i].pos - I;
                
                 //float a = dot(N, normalize(s_ray_dir));
                
                 //if (a <= 0)
                 //    continue;
                
                 //float angle = dot(normalize(-spot_dir), normalize(s_ray_dir));
                
                 //if (angle <= lights[i].inner_angle) // Outside
                 //    continue;
                
                 //float spot_value = (angle - lights[i].inner_angle) / (lights[i].outer_angle - lights[i].inner_angle);
                
                 //Ray shadow_ray = Ray(I, s_ray_dir);
                 //if (scene.IsOccluded(shadow_ray))
                 //    continue;
                
                 //final_color += albedo * lights[i].color * spot_value * a;
            }
            break;
            case LightType::AREA:
            {
                // Monte Carlo Integration
                float randomised_f = RandomFloat();

                float x = lights[i].radius * cosf(randomised_f) * sinf(randomised_f);
                float y = lights[i].radius * sinf(randomised_f) * sinf(randomised_f);
                float z = lights[i].radius * cosf(randomised_f);
                float3 new_pos = lights[i].pos + float3(x, y, z);

                float3 s_ray_dir = normalize(new_pos - I);
                float angle = dot(N, s_ray_dir);
                float dist = length(new_pos - I);
                float falloff = max(1 / (dist * dist) - 0.25f, 0.0f);

                if (angle <= 0)
                    continue;

                Ray shadow_ray = Ray(new_pos, -s_ray_dir, dist);
                if (scene.IsOccluded(shadow_ray, GRIDLAYERS))
                    continue;

                const float pdf = PI * 4 * 2 * lights[i].radius;

                final_color += albedo * lights[i].color * falloff * angle / pdf;
            }
            break;
            case LightType::LINE:
            {
                // Monte Carlo Integration
                float randomised_f = RandomFloat();

                float3 new_pos = lerp(point_a, point_b, randomised_f);

                float3 s_ray_dir = normalize(new_pos - I);
                float angle = dot(N, s_ray_dir);
                float dist = length(new_pos - I);
                float falloff = max(1 / (dist * dist) - 0.25f, 0.0f);

                if (angle <= 0)
                    continue;

                Ray shadow_ray = Ray(new_pos, -s_ray_dir, dist);
                if (scene.IsOccluded(shadow_ray, GRIDLAYERS))
                    continue;
                final_color += albedo * lights[i].color * falloff * angle;
            }
            break;
        default:
            break;
        }
    }

    // Reflections (Source: https://jacco.ompf2.com/2022/05/27/how-to-build-a-bvh-part-8-whitted-style/)
    float3 dir = normalize(ray.D);
    float3 sec_D = dir - 2 * N * dot(N, dir);
    float3 sec_O = I + N * 0.001f;

    uint random_val = RandomUInt();
    sec_D += diffusereflection(N, random_val) * roughness;

    Ray secondary = Ray(sec_O, normalize(sec_D));
    secondary.depth = ray.depth + 1;

    if (secondary.depth >= 20)
        return float3(0.0f);
    return Trace(secondary);

    // Ray shadow_ray = Ray(I, normalize(sun_pos - I));
    /* visualize normal */   // return (N + 1) * 0.5f;
    /* visualize distance */ // return float3( 1 / (1 + ray.t) );
    /* visualize albedo */ 
    if (ray.t < lightsaber_t)
    {
        return final_color;
    }
    return final_color * (1.0f - intensity) + lightsaber_cont;
}

// -----------------------------------------------------------
// Main application tick function - Executed once per frame
// -----------------------------------------------------------
void Renderer::Tick(float deltaTime)
{
    time += deltaTime;

    if (scene.has_changed)
    {
        frames = 1;
    }

    // pixel loop
    Timer t;
    // lines are executed as OpenMP parallel tasks (disabled in DEBUG)
#pragma omp parallel for schedule(dynamic)
    for (int y = 0; y < SCRHEIGHT; y++)
    {
        // trace a primary ray for each pixel on the line
        for (int x = 0; x < SCRWIDTH; x++)
        {
            //// Generate Random Offsets Within Each Pixel
            //float x_offset = Rand(1.0f) - 0.5f;
            //float y_offset = Rand(1.0f) - 0.5f;

            //// Calculate Sample Position Within The Pixel
            //float sample_x = (float)x + 0.5f + x_offset;
            //float sample_y = (float)y + 0.5f + y_offset;

            //float4 p = float4(Trace(camera.GetPrimaryRay(sample_x, sample_y)), 0);
            //
            //if (scene.has_changed)
            //    accumulator[x + y * SCRWIDTH] = p;
            //else
            //    accumulator[x + y * SCRWIDTH] += p;
            //
            //screen->pixels[x + y * SCRWIDTH] = RGBF32_to_RGB8(&(accumulator[x + y * SCRWIDTH] / float(frames)));
            Ray r = camera.GetPrimaryRay(x, y);

            bvh.intersect_bvh(voxel_objects, r, 0);

            if (r.t < 1e34f)
            {
                screen->pixels[x + y * SCRWIDTH] = RGBF32_to_RGB8(&float4(1.0f, 1.0f, 1.0f, 0.0f));
                if (grid_view)
                    screen->pixels[x + y * SCRWIDTH] = RGBF32_to_RGB8(&float4(r.steps / 32.0f, 0.0f));
            }

            else
                screen->pixels[x + y * SCRWIDTH] = RGBF32_to_RGB8(&float4(0.0f, 0.0f, 0.0f, 0.0f));
        }
    }

    if (scene.has_changed)
        scene.has_changed = false;

    // performance report - running average - ms, MRays/s
    static float avg = 10, alpha = 1;
    avg = (1 - alpha) * avg + alpha * t.elapsed() * 1000;
    if (alpha > 0.05f)
        alpha *= 0.5f;
    float fps = 1000.0f / avg, rps = (SCRWIDTH * SCRHEIGHT) / avg;
    printf("%5.2fms (%.1ffps) - %.1fMrays/s\n", avg, fps, rps / 1000);
    // handle user input
    if (camera.HandleInput(deltaTime))
        scene.has_changed = true;

    frames++;
}

// -----------------------------------------------------------
// Update user interface (imgui)
// -----------------------------------------------------------
void Renderer::UI()
{
    ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f);
    ImGui::SliderFloat3("Point A", &point_a.x, 0.0f, 1.0f);
    ImGui::SliderFloat3("Point B", &point_b.x, 0.0f, 1.0f);
    ImGui::SliderFloat("Radius", &rad, 0.0f, 0.25f);
    ImGui::Checkbox("Activate Lightsaber", &activate_lightsaber);
    ImGui::Checkbox("Grid View", &grid_view);

    if (ImGui::Button("Add Point Light"))
    {
        lights.push_back(Light(LightType::POINT));
        scene.has_changed = true;
    }
    if (ImGui::Button("Add Directional Light"))
    {
        lights.push_back(Light(LightType::DIRECTIONAL));
        scene.has_changed = true;
    }
    if (ImGui::Button("Add Spotlight"))
    {
        lights.push_back(Light(LightType::SPOT));
        scene.has_changed = true;
    }
    if (ImGui::Button("Add Area Light"))
    {
        lights.push_back(Light(LightType::AREA));
        scene.has_changed = true;
    }
    if (ImGui::Button("Add Line Light"))
    {
        lights.push_back(Light(LightType::LINE));
        scene.has_changed = true;
    }

    for (size_t i = 0; i < lights.size(); i++)
    {
        switch (lights[i].type)
        {
        case LightType::POINT: 
            {
                std::string name = "Point Light " + std::to_string(i);
                if (ImGui::CollapsingHeader(name.c_str()))
                {
                    scene.has_changed = true;
                    ImGui::SliderFloat3("Pos", &lights[i].pos.x, 0.0f, 1.0f);
                    ImGui::ColorEdit3("Color", &lights[i].color.x, ImGuiColorEditFlags_Float);
                    if (ImGui::SmallButton("Remove"))
                        lights.erase(lights.begin() + i);
                }
            }
        break;
        case LightType::DIRECTIONAL: 
            {
                std::string name = "Directional Light " + std::to_string(i);
                if (ImGui::CollapsingHeader(name.c_str()))
                {
                    scene.has_changed = true;
                    ImGui::SliderFloat3("Direction", &lights[i].dir.x, -1.0f, 1.0f);
                    ImGui::ColorEdit3("Color", &lights[i].color.x, ImGuiColorEditFlags_Float);
                    if (ImGui::SmallButton("Remove"))
                        lights.erase(lights.begin() + i);
                }
            }
            break;
        case LightType::SPOT: 
            {
                std::string name = "Spotlight " + std::to_string(i);
                if (ImGui::CollapsingHeader(name.c_str()))
                {
                    scene.has_changed = true;
                    ImGui::SliderFloat3("Direction", &lights[i].dir.x, -1.0f, 1.0f);
                    ImGui::SliderFloat3("Pos", &lights[i].pos.x, 0.0f, 1.0f);
                    /*ImGui::SliderFloat("Inner Angle", &lights[i].inner_angle, 0.0f, 1.0f);
                    ImGui::SliderFloat("Outer Angle", &lights[i].outer_angle, 1.0f, 2.0f);*/
                    ImGui::SliderFloat("Cutoff Angle", &lights[i].cutoff_angle, 0.0f, 1.0f);
                    ImGui::SliderFloat("Spot Exponent", &lights[i].spot_exponent, 0.0f, 100.0f);
                    ImGui::ColorEdit3("Color", &lights[i].color.x, ImGuiColorEditFlags_Float);
                    if (ImGui::SmallButton("Remove"))
                        lights.erase(lights.begin() + i);
                }
            }
            break;
        case LightType::AREA: 
            {
                std::string name = "Area Light " + std::to_string(i);
                if (ImGui::CollapsingHeader(name.c_str()))
                {
                    scene.has_changed = true;
                    ImGui::SliderFloat3("Pos", &lights[i].pos.x, 0.0f, 1.0f);
                    ImGui::SliderFloat("Radius", &lights[i].radius, 0.0f, 1.0f);
                    ImGui::ColorEdit3("Color", &lights[i].color.x, ImGuiColorEditFlags_Float);
                    if (ImGui::SmallButton("Remove"))
                        lights.erase(lights.begin() + i);
                }
            }
            break;
            case LightType::LINE: 
            {
                std::string name = "Line Light " + std::to_string(i);
                if (ImGui::CollapsingHeader(name.c_str()))
                {
                    scene.has_changed = true;
                    lights[i].color = light_color;
                    ImGui::ColorEdit3("Color", &light_color.x, ImGuiColorEditFlags_Float);
                    if (ImGui::SmallButton("Remove"))
                        lights.erase(lights.begin() + i);
                }
            }
            break;
        default:
            break;
        }
    }
}

// -----------------------------------------------------------
// User wants to close down
// -----------------------------------------------------------
void Renderer::Shutdown()
{
    // save current camera
    FILE* f = fopen("camera.bin", "wb");
    fwrite(&camera, 1, sizeof(Camera), f);
    fclose(f);
}