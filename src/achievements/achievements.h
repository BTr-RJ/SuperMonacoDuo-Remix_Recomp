#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Offline RetroAchievements set (official titles, points and MemAddr logic from
   assets/achievements/<game>.json) evaluated by rcheevos on the 68000 work RAM.
   Pure read: nothing here writes to guest RAM, the CPU or the scheduler. */
void smgp_ach_init(const char *game, int persist);
void smgp_ach_frame(const uint8_t *ram);
int smgp_ach_key(int sdl_key);                                   /* 1 = consumed */
void smgp_ach_overlay(int line, uint32_t *out, int width);       /* toast and F6 panel */
int smgp_ach_visible(void);                                       /* toast or panel on screen */
unsigned smgp_ach_count(void);
unsigned smgp_ach_unlocked_count(void);
int smgp_ach_unlocked(unsigned index);
const char *smgp_ach_title(unsigned index);
int smgp_ach_panel(void);
#ifdef __cplusplus
}
#endif
