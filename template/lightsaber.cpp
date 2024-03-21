#include "precomp.h"

#define OGT_VOX_IMPLEMENTATION
#include "lib/ogt_vox.h"

#include "voxel_data.h"

#include "lightsaber.h"


Lightsaber::Lightsaber(uint8_t** grids, VoxelData* voxel_data)
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
                    voxel_data[voxel_index].color.x = scene->palette.color[voxel_index].r / 255.0f;
                    voxel_data[voxel_index].color.y = scene->palette.color[voxel_index].g / 255.0f;
                    voxel_data[voxel_index].color.z = scene->palette.color[voxel_index].b / 255.0f;
                }

                for (size_t i = 0; i < GRIDLAYERS; i++)
                {
                    uint8_t b = (1 << i);
                #if INTEL_CPU
                    grids[i][morton_encode(floor(y / b), floor(z / b), floor(x / b))] = voxel_index;
                #else
                    grids[i][(x / b) + (y / b) * (GRIDSIZE / b) + (z / b) * (GRIDSIZE / b) * (GRIDSIZE / b)] = voxel_index;
                #endif
                }
            }
        }
        
    }
}
