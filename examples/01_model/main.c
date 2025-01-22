#include <libdragon.h>
#include <rdpq_tex.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

static void unpack_rgb555_to_normalized(float n[3], uint16_t rgb555) {
    // RRRRRGGGGGBBBBBX
    // FEDCBA9876543210
    rgb555 >>= 1;
    // Extract R: bits 10-14
    int r = (rgb555 >> 10) & 0x1F;
    // Extract G: bits 5-9
    int g = (rgb555 >> 5) & 0x1F;
    // Extract B: bits 0-4
    int b = rgb555 & 0x1F;

    // Normalize to range [0, 1]
    float nr = r / 31.0f;
    float ng = g / 31.0f;
    float nb = b / 31.0f;

    // Scale to range [-1, 1]
    n[0] = (nr * 2.0f) - 1.0f; // R
    n[1] = (ng * 2.0f) - 1.0f; // G
    n[2] = (nb * 2.0f) - 1.0f; // B
}

static uint16_t conv_rgb5551(uint8_t r8, uint8_t g8, uint8_t b8, uint8_t a8) {
    uint16_t r=r8>>3, g=g8>>3, b=b8>>3, a=a8 >= 128?1:0;
    return (r<<11) | (g<<6) | (b<<1) | a;
}

#define TEX_TOP (1)
#define TEX_MID (2)
#define TEX_OUTER (3)
#define TEXTURE_COUNT (3)

static uint16_t palettes[TEXTURE_COUNT][256] __attribute__((aligned(8)));
static fm_vec3_t normals[TEXTURE_COUNT][256] __attribute__((aligned(8)));
static sprite_t* tex_top;
static sprite_t* tex_mid;
static sprite_t* tex_outer;

static void t3d_mat4_mul_dir(T3DVec3* vecOut, const T3DMat4 *mat, const T3DVec3* vec)
{
  for(uint32_t i=0; i<3; i++) {
    vecOut->v[i] = mat->m[0][i] * vec->v[0] +
                   mat->m[1][i] * vec->v[1] +
                   mat->m[2][i] * vec->v[2];
  }
}

// This is a callback for t3d_model_draw_custom, it is used when a texture in a model is set to dynamic/"reference"
void dynamic_tex_cb(void* userData, const T3DMaterial* material, rdpq_texparms_t *tileParams, rdpq_tile_t tile) {
  if(tile != TILE0)return; // this callback can happen 2 times per mesh, you are allowed to skip calls
    // debugf("dynamic tex: %lu\n", material->textureA.texReference);

  rdpq_sync_tile();
  surface_t surf;
  uint16_t* pal=0;

  rdpq_mode_tlut(TLUT_RGBA16);

  switch (material->textureA.texReference) {
    case TEX_TOP:
      surf = sprite_get_pixels(tex_top);
      pal = palettes[0];
      break;
    case TEX_MID:
      surf = sprite_get_pixels(tex_mid);
      pal = palettes[1];
      break;
    case TEX_OUTER:
      surf = sprite_get_pixels(tex_outer);
      pal = palettes[2];
      break;
    default:
    debugf("Invalid reference %lu\n", material->textureA.texReference);
    break;
  }

  rdpq_tex_upload_tlut(pal, 0, 256);
  rdpq_tex_upload(tile, &surf, tileParams);
}

void update_palette(uint16_t* pal, fm_vec3_t* normals, const uint8_t *color, const T3DVec3 *dir) {

  for (int i=0;i<256;i++) {
    fm_vec3_t* n = &normals[i];
    float NdotL = fm_vec3_dot(n, dir);
    float diffuse = fmax(NdotL, 0.0f);

    // Reflection vector
    fm_vec3_t reflectDir;
    reflectDir.x = dir->x - 2 * NdotL * n->x;
    reflectDir.y = dir->y - 2 * NdotL * n->y;
    reflectDir.z = dir->z - 2 * NdotL * n->z;

    // Normalize the reflection vector
    fm_vec3_norm(&reflectDir, &reflectDir);

    // Specular component
    const float shininess = 1.f;
    float RdotV = fm_vec3_dot(&reflectDir, dir);
    float specular = powf(fmax(RdotV, 0.0f), shininess);

    // dot = 0.5f*(dot+1.0f);
    float intensity = 1.0f * diffuse + 0.0f * specular;
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;

    int v = 255 * intensity;
    pal[i] = conv_rgb5551(v,v,v,255);
  }

  data_cache_hit_writeback_invalidate(pal, sizeof(uint16_t) * 256);
}

int main()
{
	debug_init_isviewer();
	debug_init_usblog();
	asset_init_compression(2);
  joypad_init();

  dfs_init(DFS_DEFAULT_LOCATION);

  display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);

  rdpq_init();

  t3d_init((T3DInitParams){});
  T3DViewport viewport = t3d_viewport_create();

  T3DMat4 modelMat; // matrix for our model, this is a "normal" float matrix
  t3d_mat4_identity(&modelMat);

  // Now allocate a fixed-point matrix, this is what t3d uses internally.
  // Note: this gets DMA'd to the RSP, so it needs to be uncached.
  // If you can't allocate uncached memory, remember to flush the cache after writing to it instead.
  T3DMat4FP* modelMatFP = malloc_uncached(sizeof(T3DMat4FP));

  T3DMat4 scaleMat;
  t3d_mat4_identity(&scaleMat);
  T3DMat4FP* scaleMatFP = malloc_uncached(sizeof(T3DMat4FP));
    {
      float s=0.3f;
    t3d_mat4_scale(&scaleMat, s, s, s);
    t3d_mat4_to_fixed(scaleMatFP, &scaleMat);
    }


  const T3DVec3 camPos = {{0,10.0f,20.0f}};
  const T3DVec3 camTarget = {{0,9.0f,0}};

  uint8_t colorAmbient[4] = {80, 80, 100, 0xFF};
  uint8_t colorDir[4]     = {0xEE, 0xAA, 0xAA, 0xFF};

  T3DVec3 lightDirVec = {{-1.0f, 1.0f, 1.0f}};
  t3d_vec3_norm(&lightDirVec);

  // Load a model-file, this contains the geometry and some metadata
  T3DModel *model = t3d_model_load("rom:/panel.t3dm");
  T3DModel *model2 = t3d_model_load("rom:/panel_notextures.t3dm");


  tex_top = sprite_load("rom:/normal_top_256.ci8.sprite");
  tex_mid = sprite_load("rom:/normal_mid_256.ci8.sprite");
  tex_outer = sprite_load("rom:/normal_outer_256.ci8.sprite");

  sprite_t* sprites[] = {tex_top, tex_mid, tex_outer};

  for (int i=0;i<TEXTURE_COUNT;i++) {
    assert(sprite_get_format(sprites[i]) == FMT_CI8);
    memcpy(palettes[i], sprite_get_palette(sprites[i]), sizeof(uint16_t) * 256);
    data_cache_hit_writeback_invalidate(palettes[i], sizeof(uint16_t) * 256);
  }

  for (int i=0;i<TEXTURE_COUNT;i++) {
    debugf("Palette %d:\n", i);
  for (int j=0;j<256;j++) {
      uint16_t rgb555 = palettes[i][j];
      float n[3];
      unpack_rgb555_to_normalized(n, rgb555);

      // Blender	+x	+z	+y
      // Tiny3D   +x	+y	-z
      normals[i][j] = (fm_vec3_t)
      {
        .x = n[0],
        .y = n[2],
        .z = -n[1],
      };
    }
  }

  float rotAngle = 0.0f;
  float tileOffset = 0.0f;
  bool use_normalmap = true;
  rspq_block_t *dplDraw = NULL;

  for(;;)
  {
    // ======== Update ======== //
    joypad_poll();
    joypad_inputs_t joypad = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t btn = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);

    if (btn.a) {
      use_normalmap = !use_normalmap;
    }

    // rotAngle -= 0.02f;
    if (held.c_left) rotAngle -= 0.02f;
    if (held.c_right) rotAngle += 0.02f;
    if (rotAngle < -3.14f) {
        rotAngle = 0.0f;
    }
    float modelScale = 0.1f;
    float invModelScale = 1.0f/modelScale;

    t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(65.0f), 10.0f, 150.0f);
    t3d_viewport_look_at(&viewport, &camPos, &camTarget, &(T3DVec3){{0,1,0}});

    // slowly rotate model, for more information on matrices and how to draw objects
    // see the example: "03_objects"
    t3d_mat4_from_srt_euler(&modelMat,
      (float[3]){modelScale, modelScale, modelScale},
      (float[3]){0.0f, rotAngle, 0.0f},
      (float[3]){0,0,0}
    );
    t3d_mat4_to_fixed(modelMatFP, &modelMat);

    T3DMat4 inverseModelMat;
    T3DVec3 lightDirInModelSpace;

    t3d_mat4_from_srt_euler(&inverseModelMat,
      (float[3]){invModelScale, invModelScale, invModelScale},
      (float[3]){0.0f, -rotAngle, 0.0f},
      (float[3]){0,0,0}
    );

    t3d_mat4_mul_dir(&lightDirInModelSpace, &inverseModelMat, &lightDirVec);
    t3d_vec3_norm(&lightDirInModelSpace);

    if (true) {
      debugf("lightDir:             (%.3f, %.3f, %.3f)\n",
        lightDirVec.x,
        lightDirVec.y,
        lightDirVec.z
      );
      debugf("lightDirInModelSpace: (%.3f, %.3f, %.3f)\n",
        lightDirInModelSpace.x,
        lightDirInModelSpace.y,
        lightDirInModelSpace.z
      );
    }

    for (int i = 0; i < TEXTURE_COUNT; i++) {
        update_palette(palettes[i], normals[i], colorDir, &lightDirInModelSpace);
    }

    // ======== Draw ======== //
    rdpq_attach(display_get(), display_get_zbuf());
    t3d_frame_start();
    t3d_viewport_attach(&viewport);

    t3d_screen_clear_color(RGBA32(100, 80, 80, 0xFF));
    t3d_screen_clear_depth();

    t3d_light_set_ambient(colorAmbient);
    t3d_light_set_directional(0, colorDir, &lightDirVec);
    t3d_light_set_count(1);

    if (true) {
        t3d_matrix_push(modelMatFP);

        if (use_normalmap) {
          t3d_model_draw_custom(model, (T3DModelDrawConf){
            .userData = &tileOffset,
            .dynTextureCb = dynamic_tex_cb,
          });
        } else {
          t3d_model_draw(model2);
        }

        // t3d_matrix_push(scaleMatFP);
        // t3d_model_draw(model2);
        // t3d_matrix_pop(1);

        t3d_matrix_pop(1);
    } else {
      // you can use the regular rdpq_* functions with t3d.
      // In this example, the colored-band in the 3d-model is using the prim-color,
      // even though the model is recorded, you change it here dynamically.
      // rdpq_set_prim_color(get_rainbow_color(rotAngle * 0.42f));
      if(!dplDraw) {
        rspq_block_begin();

        t3d_matrix_push(modelMatFP);
        // Draw the model, material settings (e.g. textures, color-combiner) are handled internally
        t3d_model_draw(model);
        t3d_matrix_pop(1);
        dplDraw = rspq_block_end();
      }

      // for the actual draw, you can use the generic rspq-api.
      rspq_block_run(dplDraw);
    } 

    rdpq_detach_show();
  }

  t3d_destroy();
  return 0;
}
