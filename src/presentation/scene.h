#pragma once
#include <stdint.h>
struct SDL_Renderer;
#ifdef __cplusplus
extern "C" {
#endif
void smgp_scene_begin(int game,uint64_t frame,int racing,int enabled);
void smgp_scene_renderer(struct SDL_Renderer *);
const uint32_t *smgp_gpu_compose(struct SDL_Renderer *,const uint32_t *,int stride,int width,int height,uint64_t frame);
void smgp_scene_shutdown(void);
/* Read-only observers, qualified for the SMGP World cartridge. */
void smgp_scene_observe(uint32_t pc);
void smgp_scene_vblank(void);
void smgp_scene_diagnostics(void);
void smgp_scene_formation_overlay(int line,uint32_t *row,int width);
#ifdef __cplusplus
}
#endif
