#pragma once
#include <SDL2/SDL.h>
#include <string>

enum SmgpAction { MENU_UP, MENU_DOWN, STEER_LEFT, STEER_RIGHT, THROTTLE, BRAKE,
 SHIFT_UP, SHIFT_DOWN, AUX_C, START_PAUSE, SMGP_ACTION_COUNT };
struct SmgpBinding { int key=0,kind=0,code=0,sign=0; };
// pulse_pedals: R2/L2 and right stick up/down drive throttle/brake as a per-frame
// duty cycle proportional to deflection (the cartridges only read on/off bits).
// Off: the same inputs act as ordinary digital buttons above a threshold.
struct SmgpControls { SmgpBinding bind[SMGP_ACTION_COUNT]; int deadzone=25,slot=0,stick=0,pulse_pedals=0; };
SmgpControls smgp_control_defaults();
void smgp_control_load(SmgpControls&,const wchar_t *path);
bool smgp_control_save(const SmgpControls&,const wchar_t *path);
const wchar_t *smgp_action_label(int action);
std::wstring smgp_key_label(int scancode);
std::wstring smgp_pad_label(const SmgpBinding&);
bool smgp_key_allowed(int scancode);
bool smgp_pad_held(SDL_GameController*,const SmgpBinding&,int deadzone);
