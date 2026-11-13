#include "controls.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <SDL2/SDL.h>
extern "C" {
#include "input_map.h"
int gamepad_controller_instance(int slot);
}
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

SmgpControls g_smgp_controls=smgp_control_defaults();
static bool in_race=false;
static unsigned native_bits[4]={0,1,4,6}; // downshift, upshift, throttle, brake
extern "C" void smgp_controls_load(const char *){
 _putenv_s("RR_CONTROLS_PROFILE","1"); // Gear buttons never trigger SDK save/load/turbo shortcuts.
 wchar_t path[32768]={};GetEnvironmentVariableW(L"SMGP_CONTROLS_FILE",path,32768);
 smgp_control_load(g_smgp_controls,path);
 g_input_map.p[0].controller_slot=g_smgp_controls.slot;
 g_input_map.p[0].device=INPUT_DEV_BOTH;g_input_map.p[0].pad_type=PAD_3BUTTON;
 std::fprintf(stderr,"[SMGP CONTROLS] mapped keyboard and controller loaded; slot=%d deadzone=%d; vertical stick is menu-only\n",g_smgp_controls.slot,g_smgp_controls.deadzone);
 std::fprintf(stderr,"[SMGP CONTROLS] shift_up key=%d kind=%d code=%d; shift_down key=%d kind=%d code=%d\n",g_smgp_controls.bind[SHIFT_UP].key,g_smgp_controls.bind[SHIFT_UP].kind,g_smgp_controls.bind[SHIFT_UP].code,g_smgp_controls.bind[SHIFT_DOWN].key,g_smgp_controls.bind[SHIFT_DOWN].kind,g_smgp_controls.bind[SHIFT_DOWN].code);
}
// Pedals: R2/L2 and right stick up/down, race only. Deflection 0..1.
static bool pedal_on[2];static double pedal_acc[2];
static double pedal_amount(SDL_GameController*c,int brake){
 if(std::getenv("SMGP_QA_INPUT_ONLY")){ // deterministic QA values "throttle,brake" in percent
  const char*q=std::getenv("SMGP_QA_PEDALS");if(!q)return 0;double t=0,b=0;std::sscanf(q,"%lf,%lf",&t,&b);
  return std::clamp((brake?b:t)/100.0,0.0,1.0);
 }
 if(!c||!SDL_GameControllerGetAttached(c))return 0;
 double trigger=SDL_GameControllerGetAxis(c,brake?SDL_CONTROLLER_AXIS_TRIGGERLEFT:SDL_CONTROLLER_AXIS_TRIGGERRIGHT)/32767.0;
 trigger=std::clamp((trigger-.08)/.92,0.0,1.0);
 double dz=g_smgp_controls.deadzone/100.0,y=SDL_GameControllerGetAxis(c,SDL_CONTROLLER_AXIS_RIGHTY)/32767.0;
 double stick=std::clamp(((brake?y:-y)-dz)/(1.0-dz),0.0,1.0);
 return std::max(trigger,stick);
}
extern "C" void smgp_controls_context(int race,int game,const unsigned char *ram){
 // The pause menu keeps the race layout on screen, so `race` stays set. Its
 // cursor needs the menu bits (up/down), not the steering/gear mapping. The
 // native pause word is World FC66, GP II FE42; nonzero while paused.
 unsigned pause_at=game?0xfe42:0xfc66;bool paused=ram&&((ram[pause_at]<<8)|ram[pause_at+1])!=0;
 in_race=race!=0&&!paused;unsigned base=game?0xff3c:0xff20;
 static const unsigned fallback[]={0,1,4,6};
 for(int i=0;i<4;i++)native_bits[i]=ram&&ram[base+i]<=6?ram[base+i]:fallback[i];
 // One call per emulated frame: advance the pedal duty cycle (sigma-delta).
 auto*c=SDL_GameControllerFromInstanceID(gamepad_controller_instance(g_smgp_controls.slot));
 for(int i=0;i<2;i++){
  double amount=in_race?pedal_amount(c,i):0;
  if(!g_smgp_controls.pulse_pedals)pedal_on[i]=amount>=.5;
  else if(amount<=.02){pedal_acc[i]=0;pedal_on[i]=false;}
  else if(amount>=.98){pedal_acc[i]=0;pedal_on[i]=true;}
  else{pedal_acc[i]+=amount;pedal_on[i]=pedal_acc[i]>=1.0;if(pedal_on[i])pedal_acc[i]-=1.0;}
  if(std::getenv("SMGP_PEDAL_TRACE")&&in_race)std::fprintf(stderr,"[SMGP PEDAL] %s amount=%.3f on=%d pulse=%d\n",i?"brake":"throttle",amount,pedal_on[i]?1:0,g_smgp_controls.pulse_pedals);
 }
}
unsigned short smgp_controls_resolve(const unsigned char *keys,SDL_GameController*c){
 auto&cfg=g_smgp_controls;int dz=cfg.deadzone*32767/100;
 auto held=[&](int a){auto &b=cfg.bind[a];return (keys&&b.key>0&&b.key<SDL_NUM_SCANCODES&&keys[b.key])||smgp_pad_held(c,b,dz);};
 unsigned short m=0;
 if(!in_race){
  if(held(MENU_UP)||(c&&SDL_GameControllerGetAxis(c,SDL_CONTROLLER_AXIS_LEFTY)<-dz))m|=1;
  if(held(MENU_DOWN)||(c&&SDL_GameControllerGetAxis(c,SDL_CONTROLLER_AXIS_LEFTY)>dz))m|=2;
 }
 int steering=c&&cfg.stick<2?SDL_GameControllerGetAxis(c,cfg.stick==0?SDL_CONTROLLER_AXIS_LEFTX:SDL_CONTROLLER_AXIS_RIGHTX):0;
 if(held(STEER_LEFT)||steering<-dz)m|=4;
 if(held(STEER_RIGHT)||steering>dz)m|=8;
 if(held(THROTTLE)||(in_race&&pedal_on[0]))m|=1u<<(in_race?native_bits[2]:4);
 if(held(BRAKE)||(in_race&&pedal_on[1]))m|=1u<<(in_race?native_bits[3]:6);
 if(in_race){bool up=held(SHIFT_UP),down=held(SHIFT_DOWN);
  if(up&&!down)m|=1u<<native_bits[1];if(down&&!up)m|=1u<<native_bits[0];
 }
 if(held(AUX_C))m|=32; // GP II still requests C on its already-visible starting grid.
 if(held(START_PAUSE))m|=128;
 return m;
}
extern "C" unsigned short smgp_controls_mask(int player){
 if(player!=0)return 0;
 auto*c=SDL_GameControllerFromInstanceID(gamepad_controller_instance(g_smgp_controls.slot));
 if(c&&!SDL_GameControllerGetAttached(c))c=nullptr;
 return smgp_controls_resolve(SDL_GetKeyboardState(nullptr),c);
}

static int analog_wheel(int previous,int native_next,unsigned guest_pad,int *wheel,int maximum_step){
 const auto&p=g_smgp_controls;auto&left=p.bind[STEER_LEFT];auto&right=p.bind[STEER_RIGHT];
 const char *enabled=std::getenv("SMGP_ANALOG");
 if(!wheel||(enabled&&*enabled=='0')||p.stick>=2)return 0;
 const char*qa_axis=std::getenv("SMGP_QA_INPUT_ONLY")?std::getenv("SMGP_QA_ANALOG_AXIS"):nullptr;
 SDL_GameController*c=nullptr;
 if(qa_axis){
  // An in-process SDL virtual controller exercises the actual native hook.
  // It never consumes or changes the owner's physical device/preferences.
  static SDL_GameController*virtual_pad=nullptr;
  if(!virtual_pad){
   SDL_VirtualJoystickDesc d{};d.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;d.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;
   d.naxes=SDL_CONTROLLER_AXIS_MAX;d.nbuttons=SDL_CONTROLLER_BUTTON_MAX;d.axis_mask=(1u<<d.naxes)-1;d.button_mask=(1u<<d.nbuttons)-1;d.name="SMGP native analog QA";
   int index=SDL_JoystickAttachVirtualEx(&d);if(index>=0)virtual_pad=SDL_GameControllerOpen(index);
   if(virtual_pad)std::fprintf(stderr,"[SMGP ANALOG QA] SDL virtual controller attached\n");
  }
  c=virtual_pad;
  if(c){SDL_JoystickSetVirtualAxis(SDL_GameControllerGetJoystick(c),p.stick==0?SDL_CONTROLLER_AXIS_LEFTX:SDL_CONTROLLER_AXIS_RIGHTX,(Sint16)std::clamp(std::atoi(qa_axis),-32768,32767));SDL_JoystickUpdate();SDL_GameControllerUpdate();}
 }else c=SDL_GameControllerFromInstanceID(gamepad_controller_instance(p.slot));
 if(!c||!SDL_GameControllerGetAttached(c))return 0;
 if(smgp_pad_held(c,left,p.deadzone*32767/100)||smgp_pad_held(c,right,p.deadzone*32767/100)||
    (!qa_axis&&(SDL_GetKeyboardState(nullptr)[left.key]||SDL_GetKeyboardState(nullptr)[right.key])))return 0;
 int axis=SDL_GameControllerGetAxis(c,p.stick==0?SDL_CONTROLLER_AXIS_LEFTX:SDL_CONTROLLER_AXIS_RIGHTX);
 int dz=p.deadzone*32767/100;
 if(std::abs(axis)<=dz)return 0;
 bool to_left=axis<0;unsigned expected=to_left?4:8;
 if((guest_pad&12)!=expected)return 0; // Script/d-pad/opposing input wins.
 double amount=(std::abs(axis)-dz)/(double)((axis<0?32768:32767)-dz);
 int desired=128+(int)std::lround((to_left?-120:120)*amount);
 // Retain the game's rate limit and 8..248 wheel range. A partial stick
 // settles at a partial wheel angle rather than ramping to full digital lock.
 int step=std::clamp(std::abs(native_next-previous),1,maximum_step);
 *wheel=std::clamp(previous+std::clamp(desired-previous,-step,step),8,248);
 static bool reported=false;
 if(!reported){std::fprintf(stderr,"[SMGP CONTROLS] proportional stick steering active; native turn rate and limits\n");reported=true;}
 if(std::getenv("SMGP_ANALOG_TRACE"))std::fprintf(stderr,"[SMGP ANALOG] axis=%d target=%d previous=%d native=%d wheel=%d\n",axis,desired,previous,native_next,*wheel);
 return 1;
}
extern "C" int smgp_analog_wheel(int previous,int native_next,unsigned guest_pad,int *wheel){return analog_wheel(previous,native_next,guest_pad,wheel,24);}
extern "C" int smgp_analog_wheel_gp2(int previous,int native_next,unsigned guest_pad,int *wheel){return analog_wheel(previous,native_next,guest_pad,wheel,255);}
