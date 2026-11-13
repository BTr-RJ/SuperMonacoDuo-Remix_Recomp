#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Returns zero for keyboard/d-pad, neutral stick or an unavailable controller. */
int smgp_analog_wheel(int previous,int native_next,unsigned guest_pad,int *wheel);
int smgp_analog_wheel_gp2(int previous,int native_next,unsigned guest_pad,int *wheel);
void smgp_controls_load(const char *settings_path);
void smgp_controls_context(int in_race,int game,const unsigned char *ram);
unsigned short smgp_controls_mask(int player);
#ifdef __cplusplus
}
#include "control_config.h"
extern SmgpControls g_smgp_controls;
unsigned short smgp_controls_resolve(const unsigned char *keys,SDL_GameController *controller);
#endif
