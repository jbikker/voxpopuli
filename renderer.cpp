#include "precomp.h"

// YOU GET:
// 1. A fast voxel renderer in plain C/C++
// 2. Normals and voxel colors
// FROM HERE, TASKS COULD BE:							FOR SUFFICIENT
// * Materials:
//   - Reflections and diffuse reflections				<===
//   - Transmission with Snell, Fresnel					<===
//   - Textures, Minecraft-style						<===
//   - Beer's Law
//   - Normal maps
//   - Emissive materials with postproc bloom
//   - Glossy reflections (BASIC)
//   - Glossy reflections (microfacet)
// * Light transport:
//   - Point lights										<===
//   - Spot lights										<===
//   - Area lights										<===
//	 - Sampling multiple lights with 1 ray
//   - Importance-sampling
//   - Image based lighting: sky
// * Camera:
//   - Depth of field									<===
//   - Anti-aliasing									<===
//   - Panini, fish-eye etc.
//   - Post-processing: now also chromatic				<===
//   - Spline cam, follow cam, fixed look-at cam
//   - Low-res cam with CRT shader
// * Scene:
//   - HDR skydome										<===
//   - Spheres											<===
//   - Smoke & trilinear interpolation
//   - Signed Distance Fields
//   - Voxel instances with transform
//   - Triangle meshes (with a BVH)
//   - High-res: nested grid
//   - Procedural art: shapes & colors
//   - Multi-threaded Perlin / Voronoi
// * Various:
//   - Object picking
//   - Ray-traced physics
//   - Profiling & optimization
// * GPU:
//   - GPU-side Perlin / Voronoi
//   - GPU rendering *not* allowed!
// * Advanced:
//   - Ambient occlusion
//   - Denoising for soft shadows
//   - Reprojection for AO / soft shadows
//   - Line lights, tube lights, ...
//   - Bilinear interpolation and MIP-mapping
// * Simple game:
//   - 3D Arkanoid										<===
//   - 3D Snake?
//   - 3D Tank Wars for two players
//   - Chess
// REFERENCE IMAGES:
// https://www.rockpapershotgun.com/minecraft-ray-tracing
// https://assetsio.reedpopcdn.com/javaw_2019_04_20_23_52_16_879.png
// https://www.pcworld.com/wp-content/uploads/2023/04/618525e8fa47b149230.56951356-imagination-island-1-on-100838323-orig.jpg

// -----------------------------------------------------------
// Initialize the renderer
// -----------------------------------------------------------
void Renderer::Init()
{
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
}

// -----------------------------------------------------------
// Evaluate light transport
// -----------------------------------------------------------
float3 Renderer::Trace(Ray& ray)
{
    scene.FindNearest(ray);
    if (ray.voxel == 0)
        return float3(0); // or a fancy sky color
    float3 I = ray.O + (ray.t - 0.00001f) * ray.D;
    const float3 L = normalize(float3(1, 4, 0.5f));
    float3 N = ray.GetNormal();
    float3 albedo = /*ray.GetAlbedo()*/ float3(1.0f);

    float3 final_color = float3(0);

    for (size_t i = 0; i < lights.size(); i++)
    {
        switch (lights[i].type)
        {
        case LightType::POINT:
            // Rotation
            // lights[i].pos.x = sinf(time * 0.001f) * 512.0f;
            // lights[i].pos.z = cosf(time * 0.001f) * 512.0f;
            {
                // Soft Shadows
                /* float randomised_f = RandomFloat();

                 float x = 0.01f * cosf(randomised_f) * sinf(randomised_f);
                 float y = 0.01f * sinf(randomised_f) * sinf(randomised_f);
                 float z = 0.01f * cosf(randomised_f);
                 float3 new_pos = lights[i].pos + float3(x, y, z);*/

                float3 s_ray_dir = lights[i].pos - I;
                float angle = dot(N, normalize(s_ray_dir));
                float dist = length(lights[i].pos - I);
                float falloff = max(1 / (dist * dist) - 0.25f, 0.0f);

                if (angle <= 0)
                    continue;

                Ray shadow_ray = Ray(I, s_ray_dir);
                if (scene.IsOccluded(shadow_ray))
                    continue;
                final_color += albedo * lights[i].color * falloff * angle;
            }
            break;
        case LightType::DIRECTIONAL: 
            {
                float angle = dot(N, normalize(-lights[i].dir));

                if (angle <= 0)
                    continue;

                Ray shadow_ray = Ray(I, -lights[i].dir);
                if (scene.IsOccluded(shadow_ray))
                    continue;
                final_color += albedo * lights[i].color * angle;
            }
        break;
            case LightType::SPOT: 
            {
                // Source: https://math.hws.edu/graphicsbook/c7/s2.html#webgl3d.2.6
                // NOTE: Not very performant!!!
                float spot_factor = 1.0f;

                float3 spot_dir = lights[i].dir;
                float3 s_ray_dir = lights[i].pos - I;

                float a = dot(N, normalize(s_ray_dir));
                if (a <= 0)
                    continue;

                if (lights[i].cutoff_angle <= 0.0f)
                    continue;

                float angle = dot(normalize(spot_dir), normalize(s_ray_dir));

                if (angle >= lights[i].cutoff_angle)
                    spot_factor = powf(angle, lights[i].spot_exponent); 
                else
                    spot_factor = 0.0f;

                Ray shadow_ray = Ray(I, s_ray_dir);
                if (scene.IsOccluded(shadow_ray))
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
        default:
            break;
        }
    }

    // Ray shadow_ray = Ray(I, normalize(sun_pos - I));
    /* visualize normal */   // return (N + 1) * 0.5f;
    /* visualize distance */ // return float3( 1 / (1 + ray.t) );
    /* visualize albedo */ return final_color;
}

// -----------------------------------------------------------
// Main application tick function - Executed once per frame
// -----------------------------------------------------------
void Renderer::Tick(float deltaTime)
{
    time += deltaTime;

    if (IsKeyDown(GLFW_KEY_Q))
        lights[0].pos = camera.camPos;

    // pixel loop
    Timer t;
    // lines are executed as OpenMP parallel tasks (disabled in DEBUG)
#pragma omp parallel for schedule(dynamic)
    for (int y = 0; y < SCRHEIGHT; y++)
    {
        // trace a primary ray for each pixel on the line
        for (int x = 0; x < SCRWIDTH; x++)
        {
            float4 pixel = float4(Trace(camera.GetPrimaryRay((float)x, (float)y)), 0);
            // translate accumulator contents to rgb32 pixels
            screen->pixels[x + y * SCRWIDTH] = RGBF32_to_RGB8(&pixel);
            accumulator[x + y * SCRWIDTH] = pixel;
        }
    }
    // performance report - running average - ms, MRays/s
    static float avg = 10, alpha = 1;
    avg = (1 - alpha) * avg + alpha * t.elapsed() * 1000;
    if (alpha > 0.05f)
        alpha *= 0.5f;
    float fps = 1000.0f / avg, rps = (SCRWIDTH * SCRHEIGHT) / avg;
    printf("%5.2fms (%.1ffps) - %.1fMrays/s\n", avg, fps, rps / 1000);
    // handle user input
    camera.HandleInput(deltaTime);
}

// -----------------------------------------------------------
// Update user interface (imgui)
// -----------------------------------------------------------
void Renderer::UI()
{
    // ray query on mouse
    /*Ray r = camera.GetPrimaryRay( (float)mousePos.x, (float)mousePos.y );
    scene.FindNearest( r );
    ImGui::Text( "voxel: %i", r.voxel );*/
    if (ImGui::Button("Add Point Light"))
    {
        lights.push_back(Light(LightType::POINT));
    }
    if (ImGui::Button("Add Directional Light"))
    {
        lights.push_back(Light(LightType::DIRECTIONAL));
    }
    if (ImGui::Button("Add Spotlight"))
    {
        lights.push_back(Light(LightType::SPOT));
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