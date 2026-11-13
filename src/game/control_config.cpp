#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "control_config.h"
#include <algorithm>

static const wchar_t *ids[]={L"menu_up",L"menu_down",L"left",L"right",L"throttle",L"brake",L"shift_up",L"shift_down",L"aux_c",L"start"};
const wchar_t *smgp_action_label(int n){
 static const wchar_t *names[]={L"Menu: up",L"Menu: down",L"Steer left",L"Steer right",L"Accelerate / button B",L"Brake / button A",L"Shift up",L"Shift down",L"Button C / confirm",L"Start / pause"};
 return n>=0&&n<SMGP_ACTION_COUNT?names[n]:L"";
}
SmgpControls smgp_control_defaults(){
 SmgpControls c;
 const int keys[]={SDL_SCANCODE_UP,SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT,SDL_SCANCODE_X,SDL_SCANCODE_Z,SDL_SCANCODE_E,SDL_SCANCODE_Q,SDL_SCANCODE_C,SDL_SCANCODE_RETURN};
 const int buttons[]={SDL_CONTROLLER_BUTTON_DPAD_UP,SDL_CONTROLLER_BUTTON_DPAD_DOWN,SDL_CONTROLLER_BUTTON_DPAD_LEFT,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_B,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,SDL_CONTROLLER_BUTTON_LEFTSHOULDER,SDL_CONTROLLER_BUTTON_X,SDL_CONTROLLER_BUTTON_START};
 for(int a=0;a<SMGP_ACTION_COUNT;a++)c.bind[a]={keys[a],1,buttons[a],0};
 return c;
}
bool smgp_key_allowed(int sc){return sc>0&&sc<SDL_NUM_SCANCODES&&sc!=SDL_SCANCODE_ESCAPE&&sc!=SDL_SCANCODE_TAB&&!(sc>=SDL_SCANCODE_F1&&sc<=SDL_SCANCODE_F12);}
void smgp_control_load(SmgpControls &c,const wchar_t *path){
 c=smgp_control_defaults();if(!path||!*path)return;
 auto get=[&](const std::wstring &key,int value){return (int)GetPrivateProfileIntW(L"controls",key.c_str(),value,path);};
 c.deadzone=std::clamp(get(L"deadzone",c.deadzone),5,50);c.slot=std::clamp(get(L"controller_slot",0),0,5);
 c.stick=std::clamp(get(L"steering_stick",0),0,2);c.pulse_pedals=get(L"pulse_pedals",0)?1:0;
 for(int a=0;a<SMGP_ACTION_COUNT;a++){
  auto &b=c.bind[a];std::wstring id=ids[a];int key=get(id+L"_key",b.key);
  if(key==0||smgp_key_allowed(key))b.key=key;
  int kind=get(id+L"_kind",b.kind),code=get(id+L"_code",b.code),sign=get(id+L"_sign",b.sign);
  if(kind==0)b.kind=0;
  else if(kind==1&&code>=0&&code<SDL_CONTROLLER_BUTTON_MAX){b.kind=1;b.code=code;b.sign=0;}
  else if(kind==2&&code>=0&&code<SDL_CONTROLLER_AXIS_MAX&&(sign==-1||sign==1)){
   b.kind=2;b.code=code;b.sign=sign;
  }
 }
}
bool smgp_control_save(const SmgpControls&c,const wchar_t *path){
 std::wstring temporary=std::wstring(path)+L".tmp";
 bool ok=true;auto put=[&](const std::wstring&k,int v){if(!WritePrivateProfileStringW(L"controls",k.c_str(),std::to_wstring(v).c_str(),temporary.c_str()))ok=false;};
 put(L"version",1);put(L"deadzone",c.deadzone);put(L"controller_slot",c.slot);
 put(L"steering_stick",c.stick);put(L"pulse_pedals",c.pulse_pedals);
 for(int a=0;a<SMGP_ACTION_COUNT;a++){auto&b=c.bind[a];std::wstring id=ids[a];put(id+L"_key",b.key);put(id+L"_kind",b.kind);put(id+L"_code",b.code);put(id+L"_sign",b.sign);}
 WritePrivateProfileStringW(nullptr,nullptr,nullptr,temporary.c_str());
 return ok&&MoveFileExW(temporary.c_str(),path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
static std::wstring wide(const char*s){if(!s)return L"";int n=MultiByteToWideChar(CP_UTF8,0,s,-1,nullptr,0);std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,s,-1,w.data(),n);if(n)w.pop_back();return w;}
std::wstring smgp_key_label(int sc){return sc?wide(SDL_GetScancodeName((SDL_Scancode)sc)):L"No key";}
std::wstring smgp_pad_label(const SmgpBinding&b){
 if(b.kind==0)return L"No button";
 if(b.kind==1){static const wchar_t *names[]={L"A / Cross",L"B / Circle",L"X / Square",L"Y / Triangle",L"Back / Select",L"Guide",L"Start / Options",L"L3",L"R3",L"LB / L1",L"RB / R1",L"D-pad up",L"D-pad down",L"D-pad left",L"D-pad right"};
  if(b.code>=0&&b.code<15)return names[b.code];return wide(SDL_GameControllerGetStringForButton((SDL_GameControllerButton)b.code));}
 static const wchar_t *axis[]={L"Left stick horizontal",L"Left stick vertical",L"Right stick horizontal",L"Right stick vertical",L"LT / L2",L"RT / R2"};
 return b.code>=0&&b.code<6?std::wstring(axis[b.code])+(b.code<4?(b.sign<0?L" −":L" +"):L""):L"Eixo";
}
bool smgp_pad_held(SDL_GameController*c,const SmgpBinding&b,int dz){
 if(!c||!SDL_GameControllerGetAttached(c))return false;
 if(b.kind==1)return SDL_GameControllerGetButton(c,(SDL_GameControllerButton)b.code)!=0;
 if(b.kind==2){int v=SDL_GameControllerGetAxis(c,(SDL_GameControllerAxis)b.code);return b.sign<0?v<-dz:v>dz;}
 return false;
}
