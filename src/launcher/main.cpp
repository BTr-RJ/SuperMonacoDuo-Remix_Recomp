#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <objbase.h>
#include <filesystem>
#include <string>
#include <memory>
#include <algorithm>
#include <fstream>
#include <vector>
#include <array>
#include <cmath>
#include "control_editor.h"
#include "updates.h"
#include "install.h"

// Super Monaco Duo-Remix Recomp launcher. Layout, typography and interaction follow the
// Road Rash Trilogy Recomp launcher: two classic covers, a settings column and one
// primary action. Everything the games read at start-up is passed through the same
// environment variables as before, so the runtime is unchanged by this window.
namespace fs=std::filesystem;
using namespace Gdiplus;
#ifndef SMGP_VERSION
#define SMGP_VERSION "0.60"
#endif
#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)
static constexpr wchar_t kVersion[]=WIDEN(SMGP_VERSION);
static HWND window,dialog;
static HINSTANCE instance;
static fs::path root,assets,ini;
static int selected=0,hover=-1;
static float scale_=1.f,dpi_scale=1.f,origin_x=0.f,origin_y=0.f;
static HFONT body_font;
static HBRUSH dark_brush;
static HANDLE child,instance_lock;
static ULONG_PTR gdiplus_token;
static std::unique_ptr<PrivateFontCollection> display_fonts;
static std::unique_ptr<FontFamily> display_family,body_family;
static std::unique_ptr<Image> brand_art;
static std::array<std::unique_ptr<Image>,2> covers;
static bool qa_mode=false;
static fs::path qa_capture_path;
static std::wstring qa_page=L"main";
static int qa_width=0,qa_height=0;
static DWORD qa_exit_code=0;
static std::array<HWND,2> game_buttons;
static std::vector<HWND> buttons;
static constexpr int GAME0=100,PLAY=200,VIDEO=201,CONTROLS=204,IMPORT=205,HELP=206,UPDATES=207;
static constexpr UINT WM_QA_LAUNCH=WM_APP+31;
// Visual language shared with the Road Rash Trilogy launcher; only the accent differs.
static const Color ink(255,244,240,226),muted(255,166,169,163),accent(255,255,75,54);
static const wchar_t *titles[]={L"SUPER MONACO GP",L"AYRTON SENNA’S SUPER MONACO GP II"};
static const wchar_t *years[]={L"1990",L"1992"};
static const wchar_t *descriptions[]={
 L"Sega’s 1990 Formula 1 classic. Practice, race or take on the\nChampionship, now with continuous zoom and arcade art.",
 L"The 1992 sequel with the Senna licence, sixteen circuits, team\ncolours in the formation, analog steering and a victory theme."};
static const wchar_t *aspects[]={L"4:3",L"16:9",L"21:9",L"32:9"};
static const wchar_t *filters[]={L"Original pixels",L"Linear",L"koko-aio · Balanced",L"koko-aio · Ambilight",L"koko-aio · Classic CRT",L"Light CRT",L"Trinitron",L"Composite"};
static const wchar_t *modes[]={L"raw",L"linear",L"raw",L"raw",L"raw",L"crt",L"trinitron",L"composite"};
static const int widths[]={960,1280,1920,2560,3840},heights[]={540,720,1080,1440,2160};
static int aspect_choice=1,size_choice=1,filter_choice=0;
static bool zoom_on=true,fullscreen=false;

static std::wstring wide(const std::string& in){
 int n=MultiByteToWideChar(CP_UTF8,0,in.c_str(),-1,nullptr,0);std::wstring r(n,L'\0');
 if(n>1)MultiByteToWideChar(CP_UTF8,0,in.c_str(),-1,r.data(),n);if(n)r.pop_back();return r;}
static fs::path game_dir(int game){
 auto name=L"smgp"+std::to_wstring(game+1);
 wchar_t module[MAX_PATH]{};GetModuleFileNameW(nullptr,module,MAX_PATH);
 auto sibling=fs::path(module).parent_path()/name;
 if(fs::exists(sibling/(name+L".exe")))return sibling;
 auto p=root/L"games"/name;if(fs::exists(p/(name+L".exe")))return p;
 p=root/L"build/native20/bin"/name;if(fs::exists(p/(name+L".exe")))return p;
 return root/L"build/native06/bin"/name;}
static fs::path rom(int game){auto name=L"smgp"+std::to_wstring(game+1);auto p=root/L"roms"/(name+L".bin");
 return fs::exists(p)?p:root/L".local/build-inputs"/name/L"game.bin";}
static fs::path executable(int game){return game_dir(game)/(L"smgp"+std::to_wstring(game+1)+L".exe");}
static bool ready(int game){return fs::exists(executable(game))&&fs::exists(rom(game));}
static bool no_game_running(){return child==nullptr&&!smgp_install_busy();}

static void text(Graphics& g,const wchar_t*value,float x,float y,float w,float h,float size,Color color=ink,bool display=false){
 Font font(display?display_family.get():FontFamily::GenericSansSerif(),size,display?FontStyleBold:FontStyleRegular,UnitPixel);
 Font body(body_family?body_family.get():FontFamily::GenericSansSerif(),size,FontStyleRegular,UnitPixel);
 Font fallback(L"Segoe UI",size,display?FontStyleBold:FontStyleRegular,UnitPixel);
 SolidBrush brush(color);StringFormat format;format.SetTrimming(StringTrimmingEllipsisCharacter);
 auto chosen=display?&font:&body;
 g.DrawString(value,-1,chosen->GetLastStatus()==Ok?chosen:&fallback,RectF(x,y,w,h),&format,&brush);}
// Place the visible lettering instead of the font line box, so italic overhangs stay centred.
static RectF lettering(Graphics& g,const wchar_t*value,RectF box,float size,Color color,bool italic=false,bool centered=false){
 FontFamily fallback(L"Segoe UI");int style=FontStyleBold|(italic?FontStyleItalic:0);
 auto family=display_family&&display_family->IsStyleAvailable(style)?display_family.get():&fallback;
 GraphicsPath path;path.AddString(value,-1,family,style,size,PointF(0,0),StringFormat::GenericTypographic());
 RectF b;path.GetBounds(&b);
 float x=box.X+(centered?(box.Width-b.Width)*.5f:0),y=box.Y+(centered?(box.Height-b.Height)*.5f:0);
 Matrix m;m.Translate(x-b.X,y-b.Y);path.Transform(&m);SolidBrush brush(color);g.FillPath(&brush,&path);
 return RectF(x,y,b.Width,b.Height);}

static int ini_int(const wchar_t*k,int fallback){return GetPrivateProfileIntW(L"launcher",k,fallback,ini.c_str());}
static void save_settings(){
 if(qa_mode||!qa_capture_path.empty())return;
 auto set=[](const wchar_t*k,int v){auto s=std::to_wstring(v);WritePrivateProfileStringW(L"launcher",k,s.c_str(),ini.c_str());};
 set(L"game",selected);set(L"aspect",aspect_choice);set(L"size",size_choice);set(L"filter",filter_choice);
 set(L"zoom",zoom_on);set(L"fullscreen",fullscreen);}
static void select_game(int g){
 selected=std::clamp(g,0,1);for(auto b:game_buttons)InvalidateRect(b,nullptr,FALSE);
 SetWindowTextW(GetDlgItem(window,PLAY),ready(selected)?L"PLAY":L"INSTALL GAMES");
 InvalidateRect(window,nullptr,FALSE);save_settings();}

static void launch(){
 if(child)return;
 if(!ready(selected)){smgp_install_show(window);return;}
 auto dir=game_dir(selected),exe=executable(selected);
 save_settings();
 SetEnvironmentVariableW(L"SMGP_ASPECT",aspects[aspect_choice]);
 SetEnvironmentVariableW(L"SMGP_ORIGINAL",zoom_on?L"0":L"1");
 SetEnvironmentVariableW(L"GENESIS_SCREEN",modes[filter_choice]);SetEnvironmentVariableW(L"RR_CONTROLS_PROFILE",L"1");
 SetEnvironmentVariableW(L"SMGP_CONTROLS_FILE",(root/L".local/controls.ini").c_str());
 SetEnvironmentVariableW(L"RR_OUTPUT_WIDTH",std::to_wstring(widths[size_choice]).c_str());
 SetEnvironmentVariableW(L"RR_OUTPUT_HEIGHT",std::to_wstring(heights[size_choice]).c_str());
 static const wchar_t*presets[]={L"Monitor-Balanced.slangp",L"Monitor-Ambilight-immersive.slangp",L"Base.slangp"};
 fs::path preset;if(filter_choice>=2&&filter_choice<=4)preset=root/L"assets/shaders/koko-aio/Presets-ng"/presets[filter_choice-2];
 SetEnvironmentVariableW(L"RR_SHADER_PATH",preset.empty()?nullptr:preset.c_str());
 auto config=dir/L"settings.ini";
 WritePrivateProfileStringW(L"video",L"fullscreen",fullscreen?L"1":L"0",config.c_str());
 WritePrivateProfileStringW(L"video",L"linear_filter",filter_choice==1?L"1":L"0",config.c_str());
 WritePrivateProfileStringW(L"rewind",L"enabled",L"0",config.c_str());
 auto logs=root/L".local/logs";fs::create_directories(logs);
 auto logfile=logs/(L"smgp"+std::to_wstring(selected+1)+L"-latest.log");
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
 HANDLE log=CreateFileW(logfile.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 HANDLE nul=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
 STARTUPINFOW si{};si.cb=sizeof(si);
 if(log!=INVALID_HANDLE_VALUE&&nul!=INVALID_HANDLE_VALUE){si.dwFlags=STARTF_USESTDHANDLES;si.hStdOutput=si.hStdError=log;si.hStdInput=nul;}
 PROCESS_INFORMATION pi{};
 std::wstring command=L"\""+exe.wstring()+L"\" \""+rom(selected).wstring()+L"\" --no-launcher --pacing accurate";
 if(qa_mode){
  const wchar_t*script=_wgetenv(L"SMGP_QA_SCRIPT");const wchar_t*frames=_wgetenv(L"SMGP_QA_FRAMES");
  command+=L" --turbo --max-frames "+std::to_wstring(frames?_wtoi(frames):1500);
  if(script)command+=L" --input-script \""+std::wstring(script)+L"\"";
 }
 BOOL ok=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,si.dwFlags!=0,CREATE_NO_WINDOW,nullptr,dir.c_str(),&si,&pi);
 if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);if(nul!=INVALID_HANDLE_VALUE)CloseHandle(nul);
 for(auto k:{L"SMGP_ASPECT",L"SMGP_ORIGINAL",L"GENESIS_SCREEN",L"RR_CONTROLS_PROFILE",L"SMGP_CONTROLS_FILE",L"RR_OUTPUT_WIDTH",L"RR_OUTPUT_HEIGHT",L"RR_SHADER_PATH"})
  if(!qa_mode)SetEnvironmentVariableW(k,nullptr);
 if(!ok){MessageBoxW(window,L"Could not start the game. Check the installation files and the log in .local/logs.",L"Launch failed",MB_OK|MB_ICONERROR);return;}
 CloseHandle(pi.hThread);child=pi.hProcess;if(!qa_mode)ShowWindow(window,SW_MINIMIZE);
 SetTimer(window,1,300,nullptr);
}

// ---- Video & CRT dialog -------------------------------------------------------------------
static HWND combo_aspect,combo_size,combo_filter,check_zoom,check_full;
static float dialog_scale=1;static HFONT dialog_font;
static LRESULT CALLBACK dialog_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
 case WM_CREATE:{
  dialog_font=CreateFontW(-(int)(16*dialog_scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Barlow");
  auto control=[&](const wchar_t*type,const wchar_t*caption,DWORD style,int x,int y,int w,int h,int id){
   HWND c=CreateWindowExW(0,type,caption,WS_CHILD|WS_VISIBLE|style,(int)(x*dialog_scale),(int)(y*dialog_scale),(int)(w*dialog_scale),(int)(h*dialog_scale),hwnd,(HMENU)(INT_PTR)id,instance,nullptr);
   SendMessageW(c,WM_SETFONT,(WPARAM)dialog_font,TRUE);
   if((style&BS_TYPEMASK)==BS_AUTOCHECKBOX&&!wcscmp(type,L"BUTTON"))SetWindowTheme(c,L"",L"");return c;};
  control(L"STATIC",L"Aspect ratio",0,28,22,420,25,0);
  combo_aspect=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,28,50,420,220,1);
  for(auto a:aspects)SendMessageW(combo_aspect,CB_ADDSTRING,0,(LPARAM)a);SendMessageW(combo_aspect,CB_SETCURSEL,aspect_choice,0);
  control(L"STATIC",L"Output resolution",0,28,92,420,25,0);
  combo_size=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,28,120,420,220,2);
  for(int i=0;i<5;++i){auto l=std::to_wstring(widths[i])+L" × "+std::to_wstring(heights[i]);SendMessageW(combo_size,CB_ADDSTRING,0,(LPARAM)l.c_str());}
  SendMessageW(combo_size,CB_SETCURSEL,size_choice,0);
  control(L"STATIC",L"Display filter",0,28,162,420,25,0);
  combo_filter=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,28,190,420,220,3);
  for(auto f:filters)SendMessageW(combo_filter,CB_ADDSTRING,0,(LPARAM)f);SendMessageW(combo_filter,CB_SETCURSEL,filter_choice,0);
  check_zoom=control(L"BUTTON",L"Continuous zoom + arcade art",BS_AUTOCHECKBOX|WS_TABSTOP,28,240,420,28,4);
  SendMessageW(check_zoom,BM_SETCHECK,zoom_on?BST_CHECKED:BST_UNCHECKED,0);
  check_full=control(L"BUTTON",L"Fullscreen",BS_AUTOCHECKBOX|WS_TABSTOP,28,274,420,28,5);
  SendMessageW(check_full,BM_SETCHECK,fullscreen?BST_CHECKED:BST_UNCHECKED,0);
  control(L"STATIC",L"Turn off continuous zoom to see the original Mega Drive\nrendering. 16:9, 21:9 and 32:9 add side scenery in races.\nWithout suitable GPU support the native sprites are kept.\nCRT presets use their authors’ original parameters.",0,28,322,440,100,0);
  control(L"BUTTON",L"Save",BS_DEFPUSHBUTTON|WS_TABSTOP,332,436,132,38,IDOK);
  return 0;}
 case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORBTN:case WM_CTLCOLORLISTBOX:
  SetTextColor((HDC)wp,RGB(244,240,226));SetBkColor((HDC)wp,RGB(26,29,31));return (LRESULT)dark_brush;
 case WM_COMMAND:
  if(LOWORD(wp)==IDOK&&HIWORD(wp)==BN_CLICKED){
   aspect_choice=(int)SendMessageW(combo_aspect,CB_GETCURSEL,0,0);size_choice=(int)SendMessageW(combo_size,CB_GETCURSEL,0,0);
   filter_choice=(int)SendMessageW(combo_filter,CB_GETCURSEL,0,0);
   zoom_on=SendMessageW(check_zoom,BM_GETCHECK,0,0)==BST_CHECKED;fullscreen=SendMessageW(check_full,BM_GETCHECK,0,0)==BST_CHECKED;
   save_settings();DestroyWindow(hwnd);return 0;}
  break;
 case WM_CLOSE:DestroyWindow(hwnd);return 0;
 case WM_DESTROY:dialog=nullptr;DeleteObject(dialog_font);dialog_font=nullptr;EnableWindow(window,TRUE);
  if(qa_capture_path.empty()&&!qa_mode)SetForegroundWindow(window);InvalidateRect(window,nullptr,FALSE);return 0;
 }
 return DefWindowProcW(hwnd,msg,wp,lp);}
static void show_video_dialog(){
 if(dialog){SetForegroundWindow(dialog);return;}
 RECT r,work;GetWindowRect(window,&r);SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);EnableWindow(window,FALSE);
 dialog_scale=std::min(dpi_scale,(work.bottom-work.top)/540.f);int dw=(int)(510*dialog_scale),dh=(int)(540*dialog_scale);
 dialog=CreateWindowExW(WS_EX_DLGMODALFRAME|(qa_capture_path.empty()?0:WS_EX_NOACTIVATE),L"SMGPSettings",L"Video & CRT",WS_CAPTION|WS_SYSMENU|WS_POPUP|WS_VISIBLE,
  r.left+(r.right-r.left-dw)/2,std::max(work.top,r.top+(r.bottom-r.top-dh)/2),dw,dh,window,nullptr,instance,nullptr);}

// ---- Main window --------------------------------------------------------------------------
static void draw_button(DRAWITEMSTRUCT*item){
 Graphics g(item->hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
 float w=(float)(item->rcItem.right-item->rcItem.left),h=(float)(item->rcItem.bottom-item->rcItem.top);
 g.ScaleTransform(scale_,scale_);w/=scale_;h/=scale_;
 int id=(int)item->CtlID;bool hot=hover==id,focus=(item->itemState&ODS_FOCUS)!=0,pressed=(item->itemState&ODS_SELECTED)!=0;
 bool disabled=(item->itemState&ODS_DISABLED)!=0;
 if(id>=GAME0&&id<GAME0+2){int game=id-GAME0;bool active=selected==game;
  SolidBrush bg(active?Color(255,38,42,43):Color(255,23,26,28));g.FillRectangle(&bg,0.f,0.f,w,h);
  auto&cv=covers[game];
  if(cv&&cv->GetLastStatus()==Ok){float f=std::min((w-20)/cv->GetWidth(),(h-77)/cv->GetHeight());float cw=cv->GetWidth()*f,ch=cv->GetHeight()*f;
   g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.DrawImage(cv.get(),RectF((w-cw)*.5f,10,cw,ch));}
  SolidBrush sel(active?accent:Color(255,72,77,76));g.FillRectangle(&sel,10.f,h-61,w-20,active?3.f:1.f);
  text(g,titles[game],12,h-48,w-24,33,25,active?accent:ink,true);
  auto state=std::wstring(years[game])+(ready(game)?L" / Ready":L" / Not installed");text(g,state.c_str(),12,h-18,w-24,18,13,muted);
  if(active||focus||hot){Pen border(active?accent:Color(255,168,171,165),focus?2.f:1.f);g.DrawRectangle(&border,1.f,1.f,w-2,h-2);}
 }else{
  bool primary=id==PLAY;
  SolidBrush bg(primary?(pressed?Color(255,214,51,30):accent):(hot||focus?Color(255,49,53,53):Color(255,33,37,39)));
  g.FillRectangle(&bg,0.f,0.f,w,h);
  wchar_t label[128];GetWindowTextW(item->hwndItem,label,128);
  if(primary)lettering(g,label,RectF(18,4,w-36,h-8),32,Color(255,17,20,22),true,true);
  else text(g,label,18,(h-25)*.5f,w-36,h,18,disabled?muted:ink);
  if(focus){Pen pen(primary?Color(255,16,19,21):accent,2);g.DrawRectangle(&pen,4.f,4.f,w-8,h-8);}
 }
}
static LRESULT CALLBACK button_subclass(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
 if(msg==WM_GETDLGCODE){auto*in=(MSG*)lp;return DefSubclassProc(hwnd,msg,wp,lp)|DLGC_WANTARROWS|((in&&in->message==WM_KEYDOWN&&in->wParam==VK_RETURN)?DLGC_WANTMESSAGE:0);}
 if(msg==WM_KEYDOWN){
  if(wp==VK_LEFT||wp==VK_RIGHT){select_game((selected+1)%2);SetFocus(game_buttons[selected]);return 0;}
  if(wp==VK_RETURN){int id=GetDlgCtrlID(hwnd);if(id>=GAME0&&id<GAME0+2)launch();else SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),(LPARAM)hwnd);return 0;}}
 if(msg==WM_MOUSEMOVE){int id=GetDlgCtrlID(hwnd);if(hover!=id){hover=id;InvalidateRect(hwnd,nullptr,FALSE);}TRACKMOUSEEVENT e{sizeof e,TME_LEAVE,hwnd,0};TrackMouseEvent(&e);}
 if(msg==WM_MOUSELEAVE){hover=-1;InvalidateRect(hwnd,nullptr,FALSE);}
 return DefSubclassProc(hwnd,msg,wp,lp);}
static void layout(){
 RECT c;GetClientRect(window,&c);scale_=std::max(.3f,std::min(c.right/1280.f,c.bottom/800.f));
 origin_x=(c.right-1280.f*scale_)*.5f;origin_y=(c.bottom-800.f*scale_)*.5f;
 auto move=[&](HWND h,int x,int y,int w,int hh){MoveWindow(h,(int)(origin_x+x*scale_),(int)(origin_y+y*scale_),(int)(w*scale_),(int)(hh*scale_),TRUE);};
 for(int i=0;i<2;++i)move(game_buttons[i],42+i*454,240,426,414);
 for(size_t i=2;i<buttons.size();++i){int id=GetDlgCtrlID(buttons[i]);
  if(id==PLAY)move(buttons[i],952,692,284,58);
  else if(id==VIDEO||id==CONTROLS)move(buttons[i],952,267+(id==CONTROLS?1:0)*62,284,48);
  else if(id==IMPORT)move(buttons[i],952,545,284,42);
  else if(id==HELP)move(buttons[i],1070,44,166,40);
  else if(id==UPDATES)move(buttons[i],884,44,166,40);}
 InvalidateRect(window,nullptr,FALSE);}
static void render(HWND hwnd,HDC dc){
 RECT r;GetClientRect(hwnd,&r);Bitmap buffer(std::max(1L,r.right),std::max(1L,r.bottom),PixelFormat32bppARGB);
 Graphics g(&buffer);g.Clear(Color(255,16,19,21));g.TranslateTransform(origin_x,origin_y);g.ScaleTransform(scale_,scale_);
 g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
 SolidBrush header(Color(255,23,26,28)),rule(Color(255,62,66,65));
 g.FillRectangle(&header,0.f,0.f,1280.f,118.f);
 if(brand_art&&brand_art->GetLastStatus()==Ok){g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
  g.DrawImage(brand_art.get(),RectF(0,0,1280,175),0.f,0.f,(float)brand_art->GetWidth(),(float)brand_art->GetHeight(),UnitPixel);}
 auto title=lettering(g,L"SUPER MONACO",RectF(44,36,800,70),69,ink);
 lettering(g,L"DUO-REMIX RECOMP",RectF(title.GetRight()+14,title.GetBottom()-25,360,34),34,accent,true);
 g.FillRectangle(&rule,42.f,117.f,1194.f,1.f);
 text(g,L"CHOOSE YOUR RACE",42,148,890,65,49,ink,true);
 text(g,L"Two Mega Drive classics. Your original games. Pick a car and hit the track.",44,208,870,25,18,muted);
 text(g,L"SETTINGS",952,224,284,36,27,ink,true);
 g.FillRectangle(&rule,924.f,240.f,1.f,510.f);
 text(g,descriptions[selected],44,687,840,58,19,ink);
 std::wstring state=ready(selected)?std::wstring(L"Version ")+kVersion+L" / locally built from your ROM":L"Choose Install games to prepare both titles with your own ROMs.";
 text(g,state.c_str(),44,761,840,22,14,muted);
 text(g,L"Analog steering, pedals and keys\nare set in Controls.",952,609,284,48,15,muted);
 text(g,L"WHAT\u2019S NEW",952,398,284,26,20,ink,true);
 text(g,L"Continuous zoom with sound effects\nArcade art and more trackside objects\nTeam colours and analog steering\nVictory theme in GP II",952,430,300,100,15,muted);
 text(g,L"Arrow keys: choose     Enter: play",42,128,1000,20,14,muted);
 text(g,(std::wstring(L"v")+kVersion+L" / Windows x64").c_str(),1040,762,200,22,14,muted);
 Graphics screen(dc);screen.DrawImage(&buffer,0,0);}
static void paint(HWND hwnd){PAINTSTRUCT ps;auto dc=BeginPaint(hwnd,&ps);render(hwnd,dc);EndPaint(hwnd,&ps);}
static bool capture_window(HWND target){
 RECT r{};GetWindowRect(target,&r);int w=r.right-r.left,h=r.bottom-r.top;
 HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,w,h);
 auto previous=SelectObject(dc,bitmap);BOOL printed=PrintWindow(target,dc,2);SelectObject(dc,previous);bool saved=false;
 if(printed){Bitmap out(bitmap,nullptr);CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
  fs::create_directories(qa_capture_path.parent_path());saved=out.Save(qa_capture_path.c_str(),&png,nullptr)==Ok;}
 DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);return saved;}
static void about(HWND hwnd){
 MessageBoxW(hwnd,(std::wstring(L"Super Monaco Duo-Remix Recomp - version ")+kVersion+
  L"\n\nAn independent Windows fan project for Super Monaco GP (World) and Ayrton Senna’s Super Monaco GP II. "
  L"Translated 68000 code runs with a fallback for remaining paths; it is a hybrid recompilation, not a complete static one.\n\n"
  L"Continuous zoom, arcade art and effects, team colours, analog steering, widescreen and CRT filters. Network play is not implemented.\n\n"
  L"Classic covers: libretro-thumbnails. Fonts: Barlow / OFL. CRT: koko-aio. See CREDITS.md and THIRD_PARTY_NOTICES.md.\n\n"
  L"Support the project: ko-fi.com/btrrj").c_str(),L"About",MB_OK);}
static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 if(smgp_updates_event(hwnd,msg,wp,lp))return 0;
 switch(msg){
 case WM_CREATE:{window=hwnd;
  auto button=[&](const wchar_t*label,int id){HWND b=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,hwnd,(HMENU)(INT_PTR)id,instance,nullptr);
   SetWindowSubclass(b,button_subclass,1,0);buttons.push_back(b);return b;};
  for(int i=0;i<2;++i)game_buttons[i]=button(titles[i],GAME0+i);
  button(L"PLAY",PLAY);button(L"Video & CRT",VIDEO);button(L"Controls",CONTROLS);button(L"Install games",IMPORT);button(L"Updates",UPDATES);button(L"About",HELP);
  SetWindowTextW(GetDlgItem(hwnd,PLAY),ready(selected)?L"PLAY":L"INSTALL GAMES");layout();SetFocus(game_buttons[selected]);SetTimer(hwnd,2,150,nullptr);return 0;}
 case WM_PAINT:paint(hwnd);return 0;
 case WM_PRINTCLIENT:render(hwnd,(HDC)wp);return 0;
 case WM_ERASEBKGND:return 1;
 case WM_SIZE:if(wp!=SIZE_MINIMIZED)layout();return 0;
 case WM_GETMINMAXINFO:{auto*info=(MINMAXINFO*)lp;info->ptMinTrackSize={(LONG)(920*dpi_scale),(LONG)(600*dpi_scale)};return 0;}
 case WM_DPICHANGED:{auto r=(RECT*)lp;dpi_scale=HIWORD(wp)/96.f;SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout();return 0;}
 case WM_DRAWITEM:draw_button((DRAWITEMSTRUCT*)lp);return TRUE;
 case WM_QA_LAUNCH:selected=std::clamp((int)wp,0,1);launch();return 0;
 case WM_COMMAND:{int id=LOWORD(wp);
  if(id>=GAME0&&id<GAME0+2){select_game(id-GAME0);return 0;}
  switch(id){
  case PLAY:launch();break;
  case VIDEO:show_video_dialog();break;
  case CONTROLS:if(!child)smgp_open_control_editor(hwnd,root/L".local/controls.ini");break;
  case IMPORT:if(!child)smgp_install_show(hwnd);break;
  case UPDATES:smgp_updates_show(hwnd);break;
  case HELP:about(hwnd);break;}
  return 0;}
 case WM_TIMER:
  if(wp==2){
   if(!qa_capture_path.empty()){static int ticks=0;if(++ticks<3)return 0;KillTimer(hwnd,2);
    HWND target=smgp_install_window();if(!target)target=smgp_updates_window();if(!target)target=dialog;if(!target)target=hwnd;
    bool ok=capture_window(target);if(target!=hwnd)DestroyWindow(target);DestroyWindow(hwnd);PostQuitMessage(ok?0:4);return 0;}
   return 0;}
  if(child&&WaitForSingleObject(child,0)==WAIT_OBJECT_0){
   GetExitCodeProcess(child,&qa_exit_code);CloseHandle(child);child=nullptr;KillTimer(hwnd,1);
   if(qa_mode){std::ofstream report(root/L".local"/(L"qa-launch-smgp"+std::to_wstring(selected+1)+L".json"));
    report<<"{\"game\":"<<selected+1<<",\"exit_code\":"<<qa_exit_code<<",\"launched_by_native_ui\":true}";report.close();DestroyWindow(hwnd);return 0;}
   ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);
   if(qa_exit_code)MessageBoxW(hwnd,L"The game exited with an error. Diagnostic logs are available in the .local/logs folder.",L"Game session ended",MB_OK|MB_ICONWARNING);}
  return 0;
 case WM_KEYDOWN:if(wp==VK_LEFT||wp==VK_RIGHT)select_game((selected+1)%2);if(wp==VK_RETURN)launch();return 0;
 case WM_CLOSE:
  if(smgp_install_busy()){smgp_install_show(hwnd);return 0;}
  if(child){MessageBoxW(hwnd,L"Close the game window before exiting the launcher.",L"Game running",MB_OK);return 0;}
  DestroyWindow(hwnd);return 0;
 case WM_DESTROY:save_settings();PostQuitMessage(0);return 0;
 }
 return DefWindowProcW(hwnd,msg,wp,lp);}
int WINAPI wWinMain(HINSTANCE app,HINSTANCE,LPWSTR,int show){
 instance=app;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 wchar_t module[32768];GetModuleFileNameW(nullptr,module,32768);root=fs::path(module).parent_path();
 for(int i=0;i<4&&!fs::exists(root/L"assets");i++)root=root.parent_path();
 assets=root/L"assets/launcher";
 int ac=0;auto av=CommandLineToArgvW(GetCommandLineW(),&ac);
 // The update host runs from the NEW package after the old launcher exits.
 if(ac==6&&(!wcscmp(av[1],L"--apply-update")||!wcscmp(av[1],L"--recover-update"))){
  AddFontResourceExW((assets/L"Barlow-Regular.ttf").c_str(),FR_PRIVATE,nullptr);
  int r=smgp_updates_host(app,ac,av);LocalFree(av);return r;}
 bool qa_controls=false;fs::path qa_controls_path;
 for(int i=1;i<ac;++i){
  if(!wcscmp(av[i],L"--qa-capture")&&i+1<ac)qa_capture_path=fs::absolute(av[++i]);
  else if(!wcscmp(av[i],L"--qa-page")&&i+1<ac)qa_page=av[++i];
  else if(!wcscmp(av[i],L"--qa-size")&&i+2<ac){qa_width=_wtoi(av[++i]);qa_height=_wtoi(av[++i]);}
  else if(!wcscmp(av[i],L"--qa-launch")&&i+1<ac){qa_mode=true;selected=_wtoi(av[++i])-1;}
  else if(!wcscmp(av[i],L"--qa-controls")&&i+1<ac){qa_controls=true;qa_controls_path=fs::absolute(av[++i]);}}
 bool qa_arg=qa_mode||!qa_capture_path.empty()||qa_controls;
 LocalFree(av);
 if(qa_mode&&(selected<0||selected>1))return 2;
 if(!qa_capture_path.empty())SetEnvironmentVariableW(L"SMGP_QA_CAPTURE",L"1");
 CoInitialize(nullptr);INITCOMMONCONTROLSEX icc{sizeof icc,ICC_PROGRESS_CLASS|ICC_BAR_CLASSES};InitCommonControlsEx(&icc);
 dpi_scale=GetDpiForSystem()/96.f;
 fs::create_directories(root/L".local");ini=root/L".local/launcher.ini";
 if(!qa_arg){
  if(smgp_updates_recovery(root))return 0; // interrupted update: the recovery host takes over
  instance_lock=smgp_updates_mutex(root,true);
  if(!instance_lock){MessageBoxW(nullptr,L"The launcher or updater is already open for this installation.",L"Super Monaco Duo-Remix Recomp",MB_OK|MB_ICONINFORMATION);return 0;}}
 if(!fs::exists(assets/L"smgp1.png")){MessageBoxW(nullptr,L"Launcher artwork is missing. Extract the complete release package before opening the app.",L"Missing files",MB_OK|MB_ICONERROR);return 1;}
 if(!qa_mode)selected=std::clamp(ini_int(L"game",0),0,1);
 aspect_choice=std::clamp(ini_int(L"aspect",1),0,3);size_choice=std::clamp(ini_int(L"size",1),0,4);filter_choice=std::clamp(ini_int(L"filter",0),0,7);
 zoom_on=ini_int(L"zoom",1)!=0;fullscreen=ini_int(L"fullscreen",0)!=0;
 GdiplusStartupInput input;GdiplusStartup(&gdiplus_token,&input,nullptr);
 AddFontResourceExW((assets/L"BarlowCondensed-Bold.ttf").c_str(),FR_PRIVATE,nullptr);
 AddFontResourceExW((assets/L"Barlow-Regular.ttf").c_str(),FR_PRIVATE,nullptr);
 display_fonts=std::make_unique<PrivateFontCollection>();
 display_fonts->AddFontFile((assets/L"BarlowCondensed-Bold.ttf").c_str());
 display_fonts->AddFontFile((assets/L"BarlowCondensed-BoldItalic.ttf").c_str());
 display_fonts->AddFontFile((assets/L"Barlow-Regular.ttf").c_str());
 display_family=std::make_unique<FontFamily>(L"Barlow Condensed",display_fonts.get());
 body_family=std::make_unique<FontFamily>(L"Barlow",display_fonts.get());
 brand_art=std::make_unique<Image>((assets/L"masthead.png").c_str());
 for(int i=0;i<2;++i)covers[i]=std::make_unique<Image>((assets/(L"smgp"+std::to_wstring(i+1)+L".png")).c_str());
 body_font=CreateFontW(-(int)(18*dpi_scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Barlow");
 dark_brush=CreateSolidBrush(RGB(26,29,31));
 WNDCLASSW klass{};klass.hInstance=app;klass.lpfnWndProc=proc;klass.lpszClassName=L"SuperMonacoDuo";klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.hIcon=LoadIconW(app,MAKEINTRESOURCEW(101));RegisterClassW(&klass);
 WNDCLASSW dialog_class=klass;dialog_class.lpfnWndProc=dialog_proc;dialog_class.lpszClassName=L"SMGPSettings";dialog_class.hbrBackground=dark_brush;RegisterClassW(&dialog_class);
 RECT work;SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
 int ww=std::min((LONG)(1300*dpi_scale),work.right-work.left),hh=std::min((LONG)(850*dpi_scale),work.bottom-work.top);
 if(qa_width>=920&&qa_height>=600){ww=qa_width;hh=qa_height;}
 if(const wchar_t*q=_wgetenv(L"SMGP_QA_LAUNCHER_SIZE")){int w=0,h=0;if(swscanf_s(q,L"%dx%d",&w,&h)==2&&w>=720&&h>=560){ww=w;hh=h;}}
 HWND hwnd=CreateWindowExW(qa_arg?WS_EX_NOACTIVATE:0,klass.lpszClassName,(std::wstring(L"Super Monaco Duo-Remix Recomp - ")+kVersion).c_str(),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
  work.left+(work.right-work.left-ww)/2,work.top+(work.bottom-work.top-hh)/2,ww,hh,nullptr,nullptr,app,nullptr);
 smgp_updates_init(app,body_font,root,qa_arg,no_game_running);smgp_install_init(app,body_font,root);
 int result=0;
 if(qa_controls){ShowWindow(hwnd,SW_SHOWNOACTIVATE);result=smgp_control_editor_qa(hwnd,qa_controls_path.parent_path()/L"controls-qa.ini",qa_controls_path);DestroyWindow(hwnd);}
 else{
  ShowWindow(hwnd,qa_mode?SW_HIDE:(qa_arg?SW_SHOWNOACTIVATE:show));UpdateWindow(hwnd);
  if(!qa_arg)smgp_updates_startup(hwnd); // at most one background GitHub check per day
  if(!qa_capture_path.empty()){
   if(qa_page==L"controls")smgp_open_control_editor(hwnd,root/L".local/controls-qa.ini");
   else if(qa_page==L"install")smgp_install_show(hwnd);
   else if(qa_page==L"updates")smgp_updates_show(hwnd);
   else if(qa_page==L"video")show_video_dialog();}
  if(qa_mode)PostMessageW(hwnd,WM_QA_LAUNCH,selected,0);
  MSG m;while(GetMessageW(&m,nullptr,0,0)>0){
   if(smgp_control_editor_message(&m)||smgp_updates_message(m)||smgp_install_message(m))continue;
   if(dialog&&IsDialogMessageW(dialog,&m))continue;if(!dialog&&IsDialogMessageW(hwnd,&m))continue;
   TranslateMessage(&m);DispatchMessageW(&m);}
  result=qa_mode?(int)qa_exit_code:0;}
 smgp_updates_shutdown();if(instance_lock){ReleaseMutex(instance_lock);CloseHandle(instance_lock);}
 if(child)CloseHandle(child);
 for(auto&c:covers)c.reset();display_family.reset();body_family.reset();brand_art.reset();display_fonts.reset();
 RemoveFontResourceExW((assets/L"BarlowCondensed-Bold.ttf").c_str(),FR_PRIVATE,nullptr);
 RemoveFontResourceExW((assets/L"Barlow-Regular.ttf").c_str(),FR_PRIVATE,nullptr);
 DeleteObject(body_font);DeleteObject(dark_brush);GdiplusShutdown(gdiplus_token);CoUninitialize();return result;}
