#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <SDL2/SDL.h>
#include "controls.h"
extern "C" {
#include "input_map.h"
#include "gamepad.h"
}
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"check failed: %s at %d (%s)\n",#x,__LINE__,SDL_GetError());return 1;}}while(0)
int main(){
 SDL_SetMainReady();SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS)==0);
 SDL_VirtualJoystickDesc desc{};desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;desc.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;desc.naxes=SDL_CONTROLLER_AXIS_MAX;desc.nbuttons=SDL_CONTROLLER_BUTTON_MAX;desc.axis_mask=(1u<<desc.naxes)-1;desc.button_mask=(1u<<desc.nbuttons)-1;desc.name="SMGP mapping QA";
 int index=SDL_JoystickAttachVirtualEx(&desc);CHECK(index>=0);auto*j=SDL_JoystickOpen(index);CHECK(j);
 input_map_init_defaults();smgp_controls_load(nullptr);gamepad_init_players(INPUT_MAX_PLAYERS);
 int slot=-1,jid=SDL_JoystickInstanceID(j);for(int i=0;i<INPUT_MAX_PLAYERS;i++)if(gamepad_controller_instance(i)==jid)slot=i;CHECK(slot>=0);auto*c=SDL_GameControllerFromInstanceID(jid);CHECK(c);
 unsigned char keys[SDL_NUM_SCANCODES]={},ram[65536]={};
 for(unsigned base:{0xff20u,0xff3cu}){ram[base]=0;ram[base+1]=1;ram[base+2]=4;ram[base+3]=6;}
 g_smgp_controls=smgp_control_defaults();g_smgp_controls.slot=slot;
 auto update=[&](){SDL_JoystickUpdate();SDL_GameControllerUpdate();};
 auto axis=[&](int x,int y){SDL_JoystickSetVirtualAxis(j,SDL_CONTROLLER_AXIS_LEFTX,(Sint16)x);SDL_JoystickSetVirtualAxis(j,SDL_CONTROLLER_AXIS_LEFTY,(Sint16)y);update();};
 auto button=[&](int b,int v){SDL_JoystickSetVirtualButton(j,b,v);update();};auto mask=[&](){return smgp_controls_resolve(keys,c);};
 for(int game:{0,1}){
  smgp_controls_context(1,game,ram);button(SDL_CONTROLLER_BUTTON_A,1);
  for(int x:{-32768,0,32767})for(int y:{-32768,0,32767}){axis(x,y);CHECK(mask()==(16|(x<0?4:x>0?8:0)));}
  axis(0,0);button(SDL_CONTROLLER_BUTTON_A,0);button(SDL_CONTROLLER_BUTTON_DPAD_UP,1);CHECK(mask()==0);button(SDL_CONTROLLER_BUTTON_DPAD_UP,0);
  keys[SDL_SCANCODE_UP]=1;CHECK(mask()==0);keys[SDL_SCANCODE_UP]=0;
  keys[SDL_SCANCODE_E]=1;CHECK(mask()==2);keys[SDL_SCANCODE_E]=0;keys[SDL_SCANCODE_Q]=1;CHECK(mask()==1);keys[SDL_SCANCODE_Q]=0;
  button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,1);CHECK(mask()==2);button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,1);CHECK(mask()==0);button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,0);CHECK(mask()==1);button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,0);
  button(SDL_CONTROLLER_BUTTON_B,1);CHECK(mask()==64);button(SDL_CONTROLLER_BUTTON_B,0);
  keys[SDL_SCANCODE_C]=1;CHECK(mask()==32);keys[SDL_SCANCODE_C]=0;
  button(SDL_CONTROLLER_BUTTON_BACK,1);button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,1);CHECK(mask()==2&&gamepad_overlay_mask()==0);button(SDL_CONTROLLER_BUTTON_BACK,0);button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,0);
  smgp_controls_context(0,game,ram);axis(0,-32768);CHECK(mask()==1);axis(0,32767);CHECK(mask()==2);axis(0,0);keys[SDL_SCANCODE_C]=1;CHECK(mask()==32);keys[SDL_SCANCODE_C]=0;
 }
 for(unsigned base:{0xff20u,0xff3cu}){ram[base]=6;ram[base+1]=5;ram[base+2]=4;ram[base+3]=0;}
 smgp_controls_context(1,0,ram);keys[SDL_SCANCODE_E]=1;CHECK(mask()==32);keys[SDL_SCANCODE_E]=0;keys[SDL_SCANCODE_Q]=1;CHECK(mask()==64);keys[SDL_SCANCODE_Q]=0;keys[SDL_SCANCODE_Z]=1;CHECK(mask()==1);keys[SDL_SCANCODE_Z]=0;
 for(unsigned base:{0xff20u,0xff3cu}){ram[base]=0;ram[base+1]=1;ram[base+2]=4;ram[base+3]=6;}smgp_controls_context(1,0,ram);
 auto custom=smgp_control_defaults();custom.slot=slot;custom.deadzone=35;custom.stick=1;custom.bind[SHIFT_UP]={SDL_SCANCODE_R,1,SDL_CONTROLLER_BUTTON_Y,0};custom.bind[SHIFT_DOWN]={SDL_SCANCODE_T,1,SDL_CONTROLLER_BUTTON_BACK,0};custom.bind[BRAKE]={SDL_SCANCODE_SPACE,2,SDL_CONTROLLER_AXIS_TRIGGERLEFT,1};custom.bind[AUX_C]={};
 auto path=std::filesystem::absolute("build/validation/controls07/roundtrip.ini");std::filesystem::create_directories(path.parent_path());CHECK(smgp_control_save(custom,path.c_str()));smgp_control_load(g_smgp_controls,path.c_str());CHECK(g_smgp_controls.deadzone==35&&g_smgp_controls.stick==1&&g_smgp_controls.slot==slot);
 keys[SDL_SCANCODE_R]=1;CHECK(mask()==2);keys[SDL_SCANCODE_R]=0;keys[SDL_SCANCODE_T]=1;CHECK(mask()==1);keys[SDL_SCANCODE_T]=0;button(SDL_CONTROLLER_BUTTON_Y,1);CHECK(mask()==2);button(SDL_CONTROLLER_BUTTON_Y,0);
 SDL_JoystickSetVirtualAxis(j,SDL_CONTROLLER_AXIS_TRIGGERLEFT,32767);update();CHECK(mask()==64);SDL_JoystickSetVirtualAxis(j,SDL_CONTROLLER_AXIS_TRIGGERLEFT,-32768);update();CHECK(mask()==0);
 SDL_JoystickSetVirtualAxis(j,SDL_CONTROLLER_AXIS_RIGHTX,32767);update();CHECK(mask()==8);SDL_JoystickSetVirtualAxis(j,SDL_CONTROLLER_AXIS_RIGHTX,0);update();
 CHECK(!smgp_key_allowed(SDL_SCANCODE_TAB)&&!smgp_key_allowed(SDL_SCANCODE_ESCAPE)&&!smgp_key_allowed(SDL_SCANCODE_F7));
 g_smgp_controls=smgp_control_defaults();g_smgp_controls.slot=slot;
 auto settle=[&](int x){axis(x,0);int current=128;for(int n=0;n<100;n++){int result=0;if(!smgp_analog_wheel(current,current+(x<0?-8:8),mask(),&result))return -1;current=result;}return current;};
 int half=settle(16384),full=settle(32767),left=settle(-16384),full_left=settle(-32768);CHECK(half>140&&half<200&&full==248&&left>8&&left<110&&full_left==8);CHECK(std::abs((128-left)-(half-128))<=1);
 auto settle_gp2=[&](int x){axis(x,0);int current=128;for(int n=0;n<100;n++){int result=0;if(!smgp_analog_wheel_gp2(current,current+(x<0?-25:25),mask(),&result))return -1;current=result;}return current;};
 CHECK(settle_gp2(16384)==half&&settle_gp2(-16384)==left&&settle_gp2(32767)==248&&settle_gp2(-32768)==8);
 axis(32767,0);int gp2_wheel=0;CHECK(smgp_analog_wheel_gp2(128,158,8,&gp2_wheel)&&gp2_wheel==158);
 axis(16384,-32768);int wheel=0;CHECK(smgp_analog_wheel(128,136,mask(),&wheel));button(SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);CHECK(!smgp_analog_wheel(128,120,mask(),&wheel));button(SDL_CONTROLLER_BUTTON_DPAD_LEFT,0);
 g_smgp_controls.stick=2;CHECK(!smgp_analog_wheel(128,136,8,&wheel));g_smgp_controls.stick=0;
 SDL_JoystickDetachVirtual(index);SDL_Event e;while(SDL_PollEvent(&e))gamepad_handle_event(&e);CHECK(!smgp_analog_wheel(128,136,8,&wheel));keys[SDL_SCANCODE_X]=1;CHECK(smgp_controls_resolve(keys,nullptr)==16);
 gamepad_shutdown();SDL_JoystickClose(j);SDL_Quit();
 std::printf("{\"passed\":true,\"SDL_virtual_controller\":true,\"both_games\":true,\"all_stick_diagonals_no_brake_or_shift\":true,\"vertical_menus_preserved\":true,\"keyboard_and_RB_LB_gears\":true,\"native_control_options\":true,\"custom_key_button_axis_roundtrip\":true,\"half_right\":%d,\"full_right\":%d,\"half_left\":%d,\"full_left\":%d,\"digital_priority\":true,\"disconnect_keyboard_fallback\":true}\n",half,full,left,full_left);return 0;
}
