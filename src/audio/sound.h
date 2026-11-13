#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void smgp_sound_frame(int game,uint64_t frame,int racing);
void smgp_sound_observe(uint32_t pc);
#ifdef __cplusplus
}
#endif
