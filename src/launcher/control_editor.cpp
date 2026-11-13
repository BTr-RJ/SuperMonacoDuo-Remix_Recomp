#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "control_editor.h"
#include "control_config.h"
#include <algorithm>
#include <vector>
#include <memory>
#include <fstream>

namespace {
HWND editor,owner,notice,device_box,stick_box,deadzone_box,pulse_box,save_button;
HWND key_buttons[SMGP_ACTION_COUNT],pad_buttons[SMGP_ACTION_COUNT];
SmgpControls draft;
std::filesystem::path filename;
HFONT font;HBRUSH background;
int pending=-1;bool capture_pad=false,armed=false,qa=false;
std::vector<SDL_GameController*> pads;
struct Item{HWND h;int x,y,w,hgt;};std::vector<Item> items;
float scale=1;int ox=0,oy=0;
void close_pads(){for(auto*p:pads)SDL_GameControllerClose(p);pads.clear();}
void devices(){
 close_pads();SendMessageW(device_box,CB_RESETCONTENT,0,0);
 int slot=0;
 for(int i=0;i<SDL_NumJoysticks()&&slot<2;i++)if(SDL_IsGameController(i)){
  auto*p=SDL_GameControllerOpen(i);if(!p)continue;pads.push_back(p);
  int n=MultiByteToWideChar(CP_UTF8,0,SDL_GameControllerName(p),-1,nullptr,0);std::wstring name(n,0);
  MultiByteToWideChar(CP_UTF8,0,SDL_GameControllerName(p),-1,name.data(),n);if(n)name.pop_back();
  std::wstring label=L"Controller "+std::to_wstring(++slot)+L" — "+name;SendMessageW(device_box,CB_ADDSTRING,0,(LPARAM)label.c_str());
 }
 while(slot<2){std::wstring label=L"Controller "+std::to_wstring(++slot)+L" — disconnected";SendMessageW(device_box,CB_ADDSTRING,0,(LPARAM)label.c_str());}
 SendMessageW(device_box,CB_SETCURSEL,draft.slot,0);
}
SDL_GameController *selected_pad(){return draft.slot<(int)pads.size()&&SDL_GameControllerGetAttached(pads[draft.slot])?pads[draft.slot]:nullptr;}
HWND add(const wchar_t*cls,const wchar_t*text,DWORD style,int x,int y,int w,int h,int id){
 HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,editor,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
 items.push_back({c,x,y,w,h});return c;
}
void layout(){
 RECT r;GetClientRect(editor,&r);scale=std::max(.6f,std::min(r.right/820.f,r.bottom/770.f));ox=(int)((r.right-820*scale)/2);oy=(int)((r.bottom-770*scale)/2);
 HFONT next=CreateFontW(-(int)(17*scale),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Barlow");
 for(auto&i:items){SetWindowPos(i.h,nullptr,ox+(int)(i.x*scale),oy+(int)(i.y*scale),(int)(i.w*scale),(int)(i.hgt*scale),SWP_NOZORDER|SWP_NOACTIVATE);SendMessageW(i.h,WM_SETFONT,(WPARAM)next,TRUE);}
 if(font)DeleteObject(font);font=next;InvalidateRect(editor,nullptr,TRUE);
}
void refresh(){
 for(int a=0;a<SMGP_ACTION_COUNT;a++){
  auto k=smgp_key_label(draft.bind[a].key),p=smgp_pad_label(draft.bind[a]);
  if(pending==a){if(capture_pad)p=armed?L"Press a button or move an axis…":L"Release buttons and sticks…";else k=L"Press a key…";}
  SetWindowTextW(key_buttons[a],k.c_str());SetWindowTextW(pad_buttons[a],p.c_str());
 }
 EnableWindow(save_button,pending<0);
}
void cancel_capture(){pending=-1;refresh();SetWindowTextW(notice,L"Click a key or button to change it. Changes apply when a game starts.");}
void begin_capture(int a,bool pad){pending=a;capture_pad=pad;armed=false;refresh();SetWindowTextW(notice,pad?L"Release the controls, then press the new button or move an axis. Esc cancels.":L"Press the new key. Esc cancels. Tab and F1–F12 are reserved by the game.");}
bool same_context(int a,int b){auto menu=[](int n){return n==MENU_UP||n==MENU_DOWN;};auto gear=[](int n){return n==SHIFT_UP||n==SHIFT_DOWN;};return !((menu(a)&&gear(b))||(menu(b)&&gear(a)));}
bool valid(){
 for(int a:{THROTTLE,BRAKE,SHIFT_UP,SHIFT_DOWN}){auto&b=draft.bind[a];if(draft.stick<2&&b.kind==2&&b.code==(draft.stick==0?SDL_CONTROLLER_AXIS_LEFTX:SDL_CONTROLLER_AXIS_RIGHTX)){
  SetWindowTextW(notice,L"That axis already steers the car. Choose another control or disable the steering stick before saving.");return false;}}
 for(int a=0;a<SMGP_ACTION_COUNT;a++)for(int b=a+1;b<SMGP_ACTION_COUNT;b++)if(same_context(a,b)){
  auto &x=draft.bind[a];auto &y=draft.bind[b];
  bool key=x.key&&x.key==y.key,pad=x.kind&&x.kind==y.kind&&x.code==y.code&&x.sign==y.sign;
  if(key||pad){std::wstring t=std::wstring(key?L"The same key":L"The same controller input")+L" is assigned to “"+smgp_action_label(a)+L"” and “"+smgp_action_label(b)+L"”. Change or clear one of them before saving.";SetWindowTextW(notice,t.c_str());return false;}
 }
 return true;
}
void tick(){
 SDL_Event e;bool changed=false;while(SDL_PollEvent(&e))if(e.type==SDL_CONTROLLERDEVICEADDED||e.type==SDL_CONTROLLERDEVICEREMOVED)changed=true;
 if(changed)devices();SDL_GameControllerUpdate();
 if(pending<0||!capture_pad)return;
 auto*c=selected_pad();if(!c){SetWindowTextW(notice,L"Controller disconnected. Reconnect it or choose another. Esc cancels.");return;}
 int button=-1,axis=-1,sign=0;
 for(int i=0;i<SDL_CONTROLLER_BUTTON_MAX;i++)if(SDL_GameControllerGetButton(c,(SDL_GameControllerButton)i)){button=i;break;}
 for(int i=0;i<SDL_CONTROLLER_AXIS_MAX;i++){int v=SDL_GameControllerGetAxis(c,(SDL_GameControllerAxis)i);if(std::abs(v)>20000){axis=i;sign=v<0?-1:1;break;}}
 if(!armed){if(button<0&&axis<0){armed=true;refresh();}return;}
 if(button>=0||axis>=0){auto&b=draft.bind[pending];b.kind=button>=0?1:2;b.code=button>=0?button:axis;b.sign=button>=0?0:sign;cancel_capture();}
}
int scancode(WPARAM vk,LPARAM lp){
 if(vk>='A'&&vk<='Z')return SDL_SCANCODE_A+(int)vk-'A';
 if(vk>='1'&&vk<='9')return SDL_SCANCODE_1+(int)vk-'1';if(vk=='0')return SDL_SCANCODE_0;
 bool ext=(lp&(1<<24))!=0;
 switch(vk){
 case VK_UP:return SDL_SCANCODE_UP;case VK_DOWN:return SDL_SCANCODE_DOWN;case VK_LEFT:return SDL_SCANCODE_LEFT;case VK_RIGHT:return SDL_SCANCODE_RIGHT;
 case VK_RETURN:return ext?SDL_SCANCODE_KP_ENTER:SDL_SCANCODE_RETURN;case VK_SPACE:return SDL_SCANCODE_SPACE;
 case VK_PRIOR:return SDL_SCANCODE_PAGEUP;case VK_NEXT:return SDL_SCANCODE_PAGEDOWN;case VK_HOME:return SDL_SCANCODE_HOME;case VK_END:return SDL_SCANCODE_END;
 case VK_INSERT:return SDL_SCANCODE_INSERT;case VK_DELETE:return SDL_SCANCODE_DELETE;case VK_BACK:return SDL_SCANCODE_BACKSPACE;
 case VK_SHIFT:return ((lp>>16)&255)==0x36?SDL_SCANCODE_RSHIFT:SDL_SCANCODE_LSHIFT;
 case VK_CONTROL:return ext?SDL_SCANCODE_RCTRL:SDL_SCANCODE_LCTRL;case VK_MENU:return ext?SDL_SCANCODE_RALT:SDL_SCANCODE_LALT;
 case VK_TAB:return SDL_SCANCODE_TAB;case VK_ESCAPE:return SDL_SCANCODE_ESCAPE;
 }
 if(vk>=VK_F1&&vk<=VK_F12)return SDL_SCANCODE_F1+(int)vk-VK_F1;
 if(vk>=VK_NUMPAD1&&vk<=VK_NUMPAD9)return SDL_SCANCODE_KP_1+(int)vk-VK_NUMPAD1;
 if(vk==VK_NUMPAD0)return SDL_SCANCODE_KP_0;
 switch(vk){case VK_ADD:return SDL_SCANCODE_KP_PLUS;case VK_SUBTRACT:return SDL_SCANCODE_KP_MINUS;case VK_MULTIPLY:return SDL_SCANCODE_KP_MULTIPLY;case VK_DIVIDE:return SDL_SCANCODE_KP_DIVIDE;case VK_DECIMAL:return SDL_SCANCODE_KP_PERIOD;}
 wchar_t name[64]={};GetKeyNameTextW((LONG)lp,name,64);
 // Punctuation/OEM keys: use the OS character, then SDL's layout-aware lookup.
 auto ch=MapVirtualKeyW((UINT)vk,MAPVK_VK_TO_CHAR)&0x7fffffff;
 return ch?SDL_GetScancodeFromKey((SDL_Keycode)ch):0;
}
void save(){
 if(pending>=0||!valid())return;
 if(!smgp_control_save(draft,filename.c_str())){SetWindowTextW(notice,L"Could not save. Check that the .local folder is writable and try again.");return;}
 DestroyWindow(editor);
}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
 case WM_CREATE:{editor=h;items.clear();pending=-1;
  add(L"STATIC",L"Keyboard and controller",0,26,20,760,30,0);
  add(L"STATIC",L"Shared by both games. Manual gears use separate commands.",0,26,55,765,48,0);
  add(L"STATIC",L"Controller",0,26,110,80,26,0);
  device_box=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,110,105,675,220,701);
  add(L"STATIC",L"Steering stick",0,26,150,125,26,0);
  stick_box=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,155,145,290,160,702);
  for(auto*t:{L"Left — horizontal only",L"Right — horizontal only",L"Disabled"})SendMessageW(stick_box,CB_ADDSTRING,0,(LPARAM)t);SendMessageW(stick_box,CB_SETCURSEL,draft.stick,0);
  add(L"STATIC",L"Deadzone",0,470,150,110,26,0);deadzone_box=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,585,145,200,240,703);
  for(int n=5;n<=50;n++){auto t=std::to_wstring(n)+L"%";SendMessageW(deadzone_box,CB_ADDSTRING,0,(LPARAM)t.c_str());}SendMessageW(deadzone_box,CB_SETCURSEL,draft.deadzone-5,0);
  // Native checkbox text cannot take the dark theme colours; use a clickable static label.
  pulse_box=add(L"BUTTON",L"",BS_AUTOCHECKBOX|WS_TABSTOP,26,176,26,26,704);
  add(L"STATIC",L"Pulse pedals: R2 accelerates, L2 brakes, right stick ↑/↓ too (proportional)",SS_NOTIFY,58,177,735,26,705);
  SendMessageW(pulse_box,BM_SETCHECK,draft.pulse_pedals?BST_CHECKED:BST_UNCHECKED,0);
  add(L"STATIC",L"Action",0,26,202,210,25,0);add(L"STATIC",L"Keyboard — click to change",0,242,202,240,25,0);add(L"STATIC",L"Controller — click to change",0,490,202,280,25,0);
  for(int a=0;a<SMGP_ACTION_COUNT;a++){int y=235+35*a;
   add(L"STATIC",smgp_action_label(a),0,26,y+4,210,30,0);
   key_buttons[a]=add(L"BUTTON",L"",BS_PUSHBUTTON|WS_TABSTOP,242,y,230,30,800+a);
   pad_buttons[a]=add(L"BUTTON",L"",BS_PUSHBUTTON|WS_TABSTOP,490,y,245,30,820+a);
   add(L"BUTTON",L"Clear",BS_PUSHBUTTON|WS_TABSTOP,745,y,60,30,840+a);
  }
  add(L"STATIC",L"The vertical axis never becomes brake or gear automatically. Proportional steering in both games.",0,26,596,765,42,0);
  notice=add(L"STATIC",L"Click a key or button to change it. Changes apply when a game starts.",0,26,642,765,55,0);
  add(L"BUTTON",L"Restore defaults",BS_PUSHBUTTON|WS_TABSTOP,26,713,200,35,901);
  add(L"BUTTON",L"Cancel",BS_PUSHBUTTON|WS_TABSTOP,505,713,130,35,IDCANCEL);
  save_button=add(L"BUTTON",L"Save controls",BS_DEFPUSHBUTTON|WS_TABSTOP,650,713,155,35,IDOK);
  devices();refresh();layout();SetTimer(h,2,40,nullptr);return 0;
 }
 case WM_COMMAND:{int id=LOWORD(wp);
  if(id>=800&&id<810)begin_capture(id-800,false);else if(id>=820&&id<830)begin_capture(id-820,true);
  else if(id>=840&&id<850){cancel_capture();draft.bind[id-840]={};refresh();}
  else if(id==901){cancel_capture();draft=smgp_control_defaults();devices();SendMessageW(stick_box,CB_SETCURSEL,draft.stick,0);SendMessageW(deadzone_box,CB_SETCURSEL,draft.deadzone-5,0);SendMessageW(pulse_box,BM_SETCHECK,draft.pulse_pedals?BST_CHECKED:BST_UNCHECKED,0);refresh();}
  else if(id==704||id==705){if(id==705)SendMessageW(pulse_box,BM_SETCHECK,SendMessageW(pulse_box,BM_GETCHECK,0,0)==BST_CHECKED?BST_UNCHECKED:BST_CHECKED,0);
   draft.pulse_pedals=SendMessageW(pulse_box,BM_GETCHECK,0,0)==BST_CHECKED?1:0;}
  else if(id==701&&HIWORD(wp)==CBN_SELCHANGE){cancel_capture();draft.slot=(int)SendMessageW(device_box,CB_GETCURSEL,0,0);}
  else if(id==702&&HIWORD(wp)==CBN_SELCHANGE)draft.stick=(int)SendMessageW(stick_box,CB_GETCURSEL,0,0);
  else if(id==703&&HIWORD(wp)==CBN_SELCHANGE)draft.deadzone=5+(int)SendMessageW(deadzone_box,CB_GETCURSEL,0,0);
  else if(id==IDOK)save();else if(id==IDCANCEL){if(pending>=0)cancel_capture();else DestroyWindow(h);}return 0;
 }
 case WM_TIMER:tick();return 0;
 case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE&&pending>=0)cancel_capture();return 0;
 case WM_SIZE:if(wp!=SIZE_MINIMIZED)layout();return 0;
 case WM_GETMINMAXINFO:{MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&mi);((MINMAXINFO*)lp)->ptMinTrackSize={std::min(800L,mi.rcWork.right-mi.rcWork.left),std::min(650L,mi.rcWork.bottom-mi.rcWork.top)};return 0;}
 case WM_DPICHANGED:{auto*r=(RECT*)lp;SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER);return 0;}
 case WM_CTLCOLORSTATIC:SetTextColor((HDC)wp,RGB(244,240,226));SetBkColor((HDC)wp,RGB(26,29,31));return (LRESULT)background;
 case WM_CLOSE:DestroyWindow(h);return 0;
 case WM_DESTROY:KillTimer(h,2);close_pads();editor=nullptr;items.clear();if(font)DeleteObject(font);font=nullptr;EnableWindow(owner,TRUE);if(!qa)SetActiveWindow(owner);return 0;
 }return DefWindowProcW(h,msg,wp,lp);
}
void picture(const std::filesystem::path&path){
 using namespace Gdiplus;RECT r;GetClientRect(editor,&r);HDC dc=GetDC(editor),mem=CreateCompatibleDC(dc);HBITMAP bmp=CreateCompatibleBitmap(dc,r.right,r.bottom);auto prev=SelectObject(mem,bmp);PrintWindow(editor,mem,PW_CLIENTONLY);Bitmap im(bmp,nullptr);
 UINT count,bytes;GetImageEncodersSize(&count,&bytes);std::unique_ptr<BYTE[]>data(new BYTE[bytes]);auto*enc=(ImageCodecInfo*)data.get();GetImageEncoders(count,bytes,enc);for(UINT n=0;n<count;n++)if(wcscmp(enc[n].MimeType,L"image/png")==0){im.Save(path.c_str(),&enc[n].Clsid);break;}
 SelectObject(mem,prev);DeleteObject(bmp);DeleteDC(mem);ReleaseDC(editor,dc);
}
}
void smgp_open_control_editor(HWND parent,const std::filesystem::path&path){
 SDL_SetMainReady();SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS);
 if(editor){SetForegroundWindow(editor);return;}owner=parent;filename=path;smgp_control_load(draft,path.c_str());if(draft.slot>1)draft.slot=0;
 static bool registered=false;if(!registered){background=CreateSolidBrush(RGB(26,29,31));WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"SmgpControlEditor";wc.lpfnWndProc=proc;wc.hbrBackground=background;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);registered=true;}
 MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(parent,MONITOR_DEFAULTTONEAREST),&mi);RECT a=mi.rcWork;float dpi=GetDpiForWindow(parent)/96.f;
 int w=std::min((int)(850*dpi),(int)(a.right-a.left)),h=std::min((int)(820*dpi),(int)(a.bottom-a.top));
 if(qa)if(auto*q=_wgetenv(L"SMGP_QA_EDITOR_SIZE")){int qw=0,qh=0;if(swscanf_s(q,L"%dx%d",&qw,&qh)==2&&qw>=800&&qh>=650){w=qw;h=qh;}}
 EnableWindow(parent,FALSE);HWND hnd=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_DLGMODALFRAME,L"SmgpControlEditor",L"Controls — Super Monaco Duo-Remix Recomp",WS_OVERLAPPEDWINDOW&~WS_MINIMIZEBOX,a.left+(a.right-a.left-w)/2,a.top+(a.bottom-a.top-h)/2,w,h,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
 ShowWindow(hnd,SW_SHOWNORMAL);UpdateWindow(hnd);SetFocus(key_buttons[THROTTLE]);
}
bool smgp_control_editor_message(MSG*m){
 if(!editor)return false;
 if(m->message==WM_KEYDOWN||m->message==WM_SYSKEYDOWN){
  if(m->wParam==VK_ESCAPE){if(pending>=0)cancel_capture();else DestroyWindow(editor);return true;}
  if(pending>=0){if(!capture_pad&&!(m->lParam&(1<<30))){int sc=scancode(m->wParam,m->lParam);if(smgp_key_allowed(sc)){draft.bind[pending].key=sc;cancel_capture();}else SetWindowTextW(notice,L"That key is reserved or not recognised. Choose another key. Esc cancels.");}return true;}
 }
 return IsDialogMessageW(editor,m)!=0;
}
int smgp_control_editor_qa(HWND parent,const std::filesystem::path&path,const std::filesystem::path&png){
 qa=true;smgp_open_control_editor(parent,path);
 // Exercise the production capture/save paths without touching the owner's config.
 SendMessageW(editor,WM_COMMAND,800+SHIFT_UP,0);MSG m{};m.hwnd=key_buttons[SHIFT_UP];m.message=WM_KEYDOWN;m.wParam='R';smgp_control_editor_message(&m);
 if(draft.bind[SHIFT_UP].key!=SDL_SCANCODE_R)return 2;
 SendMessageW(editor,WM_COMMAND,800+SHIFT_DOWN,0);m.wParam='R';smgp_control_editor_message(&m);SendMessageW(editor,WM_COMMAND,IDOK,0);if(!editor)return 8;
 SendMessageW(editor,WM_COMMAND,800+SHIFT_DOWN,0);m.wParam='Q';smgp_control_editor_message(&m);
 SendMessageW(editor,WM_COMMAND,IDOK,0);SmgpControls saved;smgp_control_load(saved,path.c_str());
 if(saved.bind[SHIFT_UP].key!=SDL_SCANCODE_R)return 3;
 smgp_open_control_editor(parent,path);
 SDL_VirtualJoystickDesc desc{};desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;desc.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;desc.naxes=SDL_CONTROLLER_AXIS_MAX;desc.nbuttons=SDL_CONTROLLER_BUTTON_MAX;desc.axis_mask=(1u<<desc.naxes)-1;desc.button_mask=(1u<<desc.nbuttons)-1;desc.name="Launcher isolated capture QA";
 int index=SDL_JoystickAttachVirtualEx(&desc);auto*j=SDL_JoystickOpen(index);if(!j)return 5;
 SDL_JoystickSetVirtualAxis(j,SDL_CONTROLLER_AXIS_TRIGGERLEFT,-32768);SDL_JoystickSetVirtualAxis(j,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,-32768);SDL_JoystickUpdate();devices();
 for(int i=0;i<(int)pads.size();i++)if(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i]))==SDL_JoystickInstanceID(j))draft.slot=i;
 SendMessageW(device_box,CB_SETCURSEL,draft.slot,0);SendMessageW(editor,WM_COMMAND,820+SHIFT_DOWN,0);tick();
 SDL_JoystickSetVirtualButton(j,SDL_CONTROLLER_BUTTON_Y,1);SDL_JoystickUpdate();tick();if(pending>=0||draft.bind[SHIFT_DOWN].code!=SDL_CONTROLLER_BUTTON_Y)return 6;
 SDL_JoystickSetVirtualButton(j,SDL_CONTROLLER_BUTTON_Y,0);SendMessageW(editor,WM_COMMAND,IDOK,0);smgp_control_load(saved,path.c_str());if(saved.bind[SHIFT_DOWN].code!=SDL_CONTROLLER_BUTTON_Y)return 7;
 SDL_JoystickClose(j);SDL_JoystickDetachVirtual(index);
 smgp_open_control_editor(parent,path);SendMessageW(editor,WM_COMMAND,901,0);UpdateWindow(editor);picture(png);SendMessageW(editor,WM_COMMAND,IDOK,0);
 smgp_open_control_editor(parent,path);SendMessageW(editor,WM_COMMAND,800+SHIFT_DOWN,0);m.wParam='T';smgp_control_editor_message(&m);SendMessageW(editor,WM_COMMAND,IDCANCEL,0);
 SmgpControls cancelled;smgp_control_load(cancelled,path.c_str());if(cancelled.bind[SHIFT_DOWN].key!=SDL_SCANCODE_Q)return 4;
 qa=false;return 0;
}
