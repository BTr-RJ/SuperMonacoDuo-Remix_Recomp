#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <SDL.h>
extern "C" {
#include "overlay_draw.h"
#include "rc_runtime.h"
}
#include "achievements.h"

namespace {
struct Achievement { uint32_t id=0; std::string title,description,memaddr; unsigned points=0; bool unlocked=false; time_t when=0; };
std::vector<Achievement> items;
std::string game_name;
rc_runtime_t runtime;
bool runtime_ready=false,persistent=false,enabled=false,panel_open=false,panel_dirty=true;
unsigned frames_seen=0,cursor=0;
const uint8_t *ram_view=nullptr;
std::filesystem::path progress_path;
std::deque<unsigned> toast_queue;
unsigned toast_ticks=0;
bool toast_dirty=false;
constexpr int PW=320,PH=148,TH=30,ROWS=8,WARMUP=180,TOAST_TICKS=360;
uint32_t panel_pixels[PW*PH],toast_pixels[PW*TH];

// ---- minimal JSON reader for the known asset layout (objects, arrays, strings, integers) ----
struct Json {
  const std::string&s;size_t i=0;bool ok=true;
  explicit Json(const std::string&text):s(text){}
  void ws(){while(i<s.size()&&(s[i]==' '||s[i]=='\n'||s[i]=='\r'||s[i]=='\t'))++i;}
  bool take(char c){ws();if(i<s.size()&&s[i]==c){++i;return true;}return false;}
  std::string str(){
    ws();std::string out;if(i>=s.size()||s[i]!='"'){ok=false;return out;}++i;
    while(i<s.size()&&s[i]!='"'){
      char c=s[i++];
      if(c=='\\'&&i<s.size()){
        char e=s[i++];
        switch(e){case 'n':out+='\n';break;case 't':out+='\t';break;case 'r':break;case 'b':break;case 'f':break;
          case 'u':{unsigned v=0;for(int k=0;k<4&&i<s.size();++k,++i)v=v*16+(isdigit((unsigned char)s[i])?s[i]-'0':(tolower(s[i])-'a'+10));out+=v<128?char(v):'?';break;}
          default:out+=e;}
      } else out+=c;
    }
    if(i<s.size())++i;else ok=false;return out;
  }
  long long num(){ws();size_t j=i;if(j<s.size()&&s[j]=='-')++j;while(j<s.size()&&isdigit((unsigned char)s[j]))++j;long long v=atoll(s.substr(i,j-i).c_str());i=j;return v;}
  void skip(){ // skip any value
    ws();if(i>=s.size()){ok=false;return;}
    if(s[i]=='"'){str();return;}
    if(s[i]=='{'||s[i]=='['){char close=s[i]=='{'?'}':']';++i;if(take(close))return;
      do{ if(close=='}'){str();take(':');} skip(); }while(take(','));take(close);return;}
    while(i<s.size()&&s[i]!=','&&s[i]!='}'&&s[i]!=']')++i;
  }
};
bool load_set(const std::filesystem::path&file){
  std::ifstream in(file,std::ios::binary);if(!in)return false;
  std::stringstream buffer;buffer<<in.rdbuf();std::string text=buffer.str();
  Json j(text);if(!j.take('{'))return false;
  do{
    std::string key=j.str();j.take(':');
    if(key!="achievements"){j.skip();continue;}
    if(!j.take('['))return false;
    if(!j.take(']')){
      do{
        Achievement a;if(!j.take('{'))return false;
        do{
          std::string k=j.str();j.take(':');
          if(k=="id")a.id=(uint32_t)j.num();else if(k=="title")a.title=j.str();else if(k=="description")a.description=j.str();
          else if(k=="memaddr")a.memaddr=j.str();else if(k=="points")a.points=(unsigned)j.num();else j.skip();
        }while(j.take(','));
        j.take('}');if(a.id&&!a.memaddr.empty())items.push_back(a);
      }while(j.take(','));
      j.take(']');
    }
  }while(j.take(','));
  return j.ok&&!items.empty();
}
std::filesystem::path exe_dir(){wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);return std::filesystem::path(exe).parent_path();}
std::filesystem::path find_set(){
  auto folder=exe_dir();
  for(int i=0;i<8;++i){
    auto candidate=folder/"assets/achievements"/(game_name+".json");
    if(std::filesystem::exists(candidate))return candidate;
    if(!folder.has_parent_path()||folder.parent_path()==folder)break;
    folder=folder.parent_path();
  }
  return {};
}
std::filesystem::path progress_file(){
  wchar_t folder[32768]={};
  if(GetEnvironmentVariableW(L"SMGP_ACHIEVEMENTS_DIR",folder,32768)&&*folder)return std::filesystem::path(folder)/(game_name+".dat");
  wchar_t controls[32768]={};
  if(GetEnvironmentVariableW(L"SMGP_CONTROLS_FILE",controls,32768)&&*controls)return std::filesystem::path(controls).parent_path()/"achievements"/(game_name+".dat");
  auto folder2=exe_dir();
  for(int i=0;i<8;++i){
    if(std::filesystem::exists(folder2/".local"))return folder2/".local/achievements"/(game_name+".dat");
    if(!folder2.has_parent_path()||folder2.parent_path()==folder2)break;
    folder2=folder2.parent_path();
  }
  return exe_dir()/"achievements"/(game_name+".dat");
}
void save_progress(){
  if(!persistent||progress_path.empty())return;
  std::error_code error;std::filesystem::create_directories(progress_path.parent_path(),error);
  auto temp=progress_path;temp+=L".tmp";
  {std::ofstream file(temp,std::ios::binary);file<<"SMGPACH1\n";for(const auto&a:items)if(a.unlocked)file<<a.id<<' '<<(long long)a.when<<'\n';if(!file)return;}
  MoveFileExW(temp.c_str(),progress_path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
void load_progress(){
  if(!persistent)return;
  std::ifstream file(progress_path);std::string magic;if(!(file>>magic)||magic!="SMGPACH1")return;
  unsigned long long id,when;
  while(file>>id>>when)for(auto&a:items)if(a.id==id){a.unlocked=true;a.when=(time_t)when;}
}
// Official Mega Drive sets read a word-swizzled 64 KiB view of the 68000 work RAM.
uint32_t peek(uint32_t address,uint8_t*buffer,uint32_t count,void*){
  if(!ram_view||!buffer||address>=0x10000u)return 0;
  if(count>0x10000u-address)count=0x10000u-address;
  for(uint32_t k=0;k<count;++k)buffer[k]=ram_view[(address+k)^1u];
  return count;
}
void on_event(const rc_runtime_event_t*event){
  if(event->type!=RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED)return;
  for(unsigned n=0;n<items.size();++n){
    if(items[n].id!=event->id||items[n].unlocked)continue;
    items[n].unlocked=true;items[n].when=time(nullptr);toast_queue.push_back(n);panel_dirty=true;save_progress();
    std::fprintf(stderr,"[SMGP ACHIEVEMENT] id=%u points=%u %s\n",items[n].id,items[n].points,items[n].title.c_str());
  }
}
std::string clip(const std::string&s,size_t n){return s.size()<=n?s:s.substr(0,n);}
void wrap(const std::string&text,size_t width,std::vector<std::string>&lines){
  std::istringstream words(text);std::string word,line;
  while(words>>word){
    if(!line.empty()&&line.size()+1+word.size()>width){lines.push_back(line);line.clear();}
    line+=(line.empty()?"":" ")+word;
  }
  if(!line.empty())lines.push_back(line);
}
void draw_panel(){
  const uint32_t gold=0xFFFFCD35u,text=0xFFF4F0E2u,dim=0xFFADB2ABu,back=0xFF171B1Eu;
  gen_ovl_fill_rect(panel_pixels,PW,PH,0,0,PW,PH,back);
  unsigned done=smgp_ach_unlocked_count(),points=0,total=0;
  for(const auto&a:items){total+=a.points;if(a.unlocked)points+=a.points;}
  char head[96];std::snprintf(head,sizeof head,"ACHIEVEMENTS %u/%u  %u/%u PTS",done,(unsigned)items.size(),points,total);
  gen_ovl_draw_text(panel_pixels,PW,PH,8,5,head,gold,1);
  gen_ovl_draw_text(panel_pixels,PW,PH,8,15,"RETROACHIEVEMENTS SET - LOCAL PROGRESS",dim,1);
  unsigned first=cursor/ROWS*ROWS;
  for(unsigned r=0;r<ROWS&&first+r<items.size();++r){
    const auto&a=items[first+r];int y=28+r*10;
    if(first+r==cursor)gen_ovl_draw_text(panel_pixels,PW,PH,2,y,">",gold,1);
    gen_ovl_draw_text(panel_pixels,PW,PH,12,y,a.unlocked?"X":"-",a.unlocked?gold:dim,1);
    char pts[8];std::snprintf(pts,sizeof pts,"%2u",a.points);
    gen_ovl_draw_text(panel_pixels,PW,PH,26,y,pts,a.unlocked?gold:dim,1);
    gen_ovl_draw_text(panel_pixels,PW,PH,50,y,clip(a.title,33).c_str(),a.unlocked?text:dim,1);
  }
  if(cursor<items.size()){
    std::vector<std::string> lines;wrap(items[cursor].description,38,lines);
    for(size_t n=0;n<lines.size()&&n<3;++n)gen_ovl_draw_text(panel_pixels,PW,PH,8,110+n*10,lines[n].c_str(),text,1);
  }
  char foot[64];std::snprintf(foot,sizeof foot,"UP/DOWN PGUP/PGDN  F6 CLOSE  P%u/%u",first/ROWS+1,(unsigned)((items.size()+ROWS-1)/ROWS));
  gen_ovl_draw_text(panel_pixels,PW,PH,8,140,foot,dim,1);
  panel_dirty=false;
}
void draw_toast(unsigned index){
  gen_ovl_fill_rect(toast_pixels,PW,TH,0,0,PW,TH,0xFF171B1Eu);
  gen_ovl_draw_text(toast_pixels,PW,TH,7,4,"ACHIEVEMENT UNLOCKED",0xFFFFCD35u,1);
  char row[96];std::snprintf(row,sizeof row,"%s (%u)",clip(items[index].title,31).c_str(),items[index].points);
  gen_ovl_draw_text(toast_pixels,PW,TH,7,16,row,0xFFF5F1E5u,1);
}
}

extern "C" void smgp_ach_init(const char*game,int persist){
  items.clear();toast_queue.clear();toast_ticks=0;panel_open=false;panel_dirty=true;frames_seen=0;cursor=0;runtime_ready=false;
  game_name=game?game:"";const char*off=std::getenv("SMGP_ACHIEVEMENTS");
  enabled=!(off&&off[0]=='0');if(!enabled)return;
  auto set=find_set();
  if(set.empty()||!load_set(set)){std::fprintf(stderr,"[SMGP ACHIEVEMENTS] set for %s not found; disabled\n",game_name.c_str());enabled=false;return;}
  const char*keep=std::getenv("SMGP_ACHIEVEMENTS_PERSIST");
  persistent=(persist!=0||std::getenv("SMGP_QA_ACH_PERSIST"))&&!(keep&&keep[0]=='0');   // scripted QA runs never persist unless asked
  progress_path=progress_file();load_progress();
  if(std::getenv("SMGP_QA_ACH_PANEL"))panel_open=true;   // QA only: capture the panel without a keyboard
  rc_runtime_init(&runtime);runtime_ready=true;
  for(const auto&a:items)if(!a.unlocked){int r=rc_runtime_activate_achievement(&runtime,a.id,a.memaddr.c_str(),nullptr,0);if(r!=RC_OK)std::fprintf(stderr,"[SMGP ACHIEVEMENTS] id=%u rejected (%d)\n",a.id,r);}
  std::fprintf(stderr,"[SMGP ACHIEVEMENTS] %s: %u achievements, %u already unlocked, persist=%d\n",game_name.c_str(),smgp_ach_count(),smgp_ach_unlocked_count(),persistent?1:0);
}
extern "C" void smgp_ach_frame(const uint8_t*ram){
  if(!enabled||!runtime_ready)return;
  ram_view=ram;++frames_seen;
  if(toast_ticks){if(!--toast_ticks)toast_dirty=false;}
  if(!toast_ticks&&!toast_queue.empty()){draw_toast(toast_queue.front());toast_queue.pop_front();toast_ticks=TOAST_TICKS;toast_dirty=true;}
  // Let the cartridge finish booting before the first evaluation: RAM is not meaningful earlier.
  if(frames_seen<WARMUP)return;
  if(frames_seen==WARMUP)rc_runtime_reset(&runtime);
  rc_runtime_do_frame(&runtime,on_event,peek,nullptr,nullptr);
}
extern "C" int smgp_ach_key(int key){
  if(!enabled)return 0;
  if(key==SDLK_F6){panel_open=!panel_open;panel_dirty=true;return 1;}
  if(!panel_open||items.empty())return 0;
  switch(key){
    case SDLK_UP:cursor=cursor?cursor-1:(unsigned)items.size()-1;break;
    case SDLK_DOWN:cursor=(cursor+1)%items.size();break;
    case SDLK_PAGEUP:cursor=cursor>=ROWS?cursor-ROWS:0;break;
    case SDLK_PAGEDOWN:cursor=std::min<unsigned>(cursor+ROWS,(unsigned)items.size()-1);break;
    default:return 0;
  }
  panel_dirty=true;return 1;
}
extern "C" void smgp_ach_overlay(int line,uint32_t*out,int width){
  if(!enabled)return;
  int start=(width-PW)/2;
  if(panel_open){
    if(panel_dirty)draw_panel();
    if(line>=38&&line<38+PH)for(int x=0;x<PW;++x)if(start+x>=0&&start+x<width)out[start+x]=panel_pixels[(line-38)*PW+x];
  }else if(toast_ticks&&toast_dirty&&line>=6&&line<6+TH){
    for(int x=0;x<PW;++x)if(start+x>=0&&start+x<width)out[start+x]=toast_pixels[(line-6)*PW+x];
  }
}
extern "C" unsigned smgp_ach_count(void){return (unsigned)items.size();}
extern "C" unsigned smgp_ach_unlocked_count(void){unsigned n=0;for(const auto&a:items)n+=a.unlocked;return n;}
extern "C" int smgp_ach_unlocked(unsigned i){return i<items.size()&&items[i].unlocked;}
extern "C" const char*smgp_ach_title(unsigned i){return i<items.size()?items[i].title.c_str():"";}
extern "C" int smgp_ach_panel(void){return panel_open;}
