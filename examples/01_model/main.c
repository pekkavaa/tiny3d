#include <libdragon.h>
#include <rdpq_tex.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

color_t get_rainbow_color(float s) {
  float r = fm_sinf(s + 0.0f) * 127.0f + 128.0f;
  float g = fm_sinf(s + 2.0f) * 127.0f + 128.0f;
  float b = fm_sinf(s + 4.0f) * 127.0f + 128.0f;
  return RGBA32(r, g, b, 255);
}

#define TEX_TOP (1)
#define TEX_MID (2)
#define TEX_OUTER (3)

static uint16_t palettes[3][256] __attribute__((aligned(8)));
static sprite_t* tex_top;
static sprite_t* tex_mid;
static sprite_t* tex_outer;

// This is a callback for t3d_model_draw_custom, it is used when a texture in a model is set to dynamic/"reference"
void dynamic_tex_cb(void* userData, const T3DMaterial* material, rdpq_texparms_t *tileParams, rdpq_tile_t tile) {
  // if(tile != TILE0)return; // this callback can happen 2 times per mesh, you are allowed to skip calls
        debugf("dynamic tex: %lu\n", material->textureA.texReference);

  // surface_t *offscreenSurf = (surface_t*)userData;
  rdpq_sync_tile();
      // rdpq_mode_push();
      // rdpq_mode_tlut(TLUT_RGBA16);

  switch (material->textureA.texReference) {
    case TEX_TOP:
      rdpq_sprite_upload(tile, tex_top, tileParams);
      break;
    case TEX_MID:
      // rdpq_sprite_upload(tile, tex_mid, tileParams);
      const surface_t surf = sprite_get_pixels(tex_mid);
      rdpq_tex_upload_tlut(palettes[1], 0, 256);
      rdpq_tex_upload(tile, &surf, tileParams);
      break;
    case TEX_OUTER:
      rdpq_sprite_upload(tile, tex_outer, tileParams);
      break;
    default:
    debugf("Invalid reference %lu\n", material->textureA.texReference);
    break;
  }

  // rdpq_mode_pop();

  // // upload a slice of the offscreen-buffer, the screen in the TV model is split into 4 materials for each section
  // // if you are working with a small enough single texture, you can ofc use a normal sprite upload.
  // // the quadrant is determined by the texture reference set in fast64, which can be used as an arbitrary value
  // switch(material->textureA.texReference) { // Note: TILE1 is used here due to CC shenanigans
  //   case 1: rdpq_tex_upload_sub(TILE1, offscreenSurf, NULL,  0,     0,     sHalf, sHalf); break;
  //   case 2: rdpq_tex_upload_sub(TILE1, offscreenSurf, NULL,  sHalf, 0,     sFull, sHalf); break;
  //   case 3: rdpq_tex_upload_sub(TILE1, offscreenSurf, NULL,  0,     sHalf, sHalf, sFull); break;
  //   case 4: rdpq_tex_upload_sub(TILE1, offscreenSurf, NULL,  sHalf, sHalf, sFull, sFull); break;
  // }
}

int main()
{
	debug_init_isviewer();
	debug_init_usblog();
	asset_init_compression(2);

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

  const T3DVec3 camPos = {{0,10.0f,20.0f}};
  const T3DVec3 camTarget = {{0,9.0f,0}};

  uint8_t colorAmbient[4] = {80, 80, 100, 0xFF};
  uint8_t colorDir[4]     = {0xEE, 0xAA, 0xAA, 0xFF};

  T3DVec3 lightDirVec = {{-1.0f, 1.0f, 1.0f}};
  t3d_vec3_norm(&lightDirVec);

  // Load a model-file, this contains the geometry and some metadata
  T3DModel *model = t3d_model_load("rom:/panel.t3dm");


  tex_top = sprite_load("rom:/normal_top_256.ci8.sprite");
  tex_mid = sprite_load("rom:/normal_mid_256.ci8.sprite");
  tex_outer = sprite_load("rom:/normal_outer_256.ci8.sprite");

  sprite_t* sprites[] = {tex_top, tex_mid, tex_outer};

  for (int i=0;i<3;i++) {
    assert(sprite_get_format(sprites[i]) == FMT_CI8);
    memcpy(palettes[i], sprite_get_palette(sprites[i]), sizeof(uint16_t) * 256);
    data_cache_hit_writeback_invalidate(palettes[i], sizeof(uint16_t) * 256);
  }

  for (int i=0;i<3;i++) {
    debugf("Palette %d:\n", i);
    for (int j=0;j<256;j++) {
      debugf("0x%x ", palettes[i][j]);
    }
    debugf("\n\n");
  }

  float rotAngle = 0.0f;
  float tileOffset = 0.0f;
  rspq_block_t *dplDraw = NULL;

  for(;;)
  {
    // ======== Update ======== //
    rotAngle -= 0.02f;
    if (rotAngle < -3.14f) {
        rotAngle = 0.0f;
    }
    float modelScale = 0.1f;

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

        t3d_model_draw_custom(model, (T3DModelDrawConf){
          .userData = &tileOffset,
          .dynTextureCb = dynamic_tex_cb,
        });
        t3d_matrix_pop(1);
    } else {
      // you can use the regular rdpq_* functions with t3d.
      // In this example, the colored-band in the 3d-model is using the prim-color,
      // even though the model is recorded, you change it here dynamically.
      rdpq_set_prim_color(get_rainbow_color(rotAngle * 0.42f));
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
