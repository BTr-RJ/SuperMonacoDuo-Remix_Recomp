// Presentation-only GPU sprite scaling. Guest SAT evaluation remains native.
#include <SDL2/SDL.h>
extern "C" {
#include "genesis_vdp.h"
#include "genesis_dac.h"
#include "genesis_runtime.h"
#include "genesis_machine.h"
#include "png_write.h"
}
#include "scene.h"
#include "achievements.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <deque>
#include <filesystem>
#include <fstream>
#include <set>
#include <map>
#include <limits>

namespace {
#include "formation_art20.h"
struct FormationUpload { std::array<uint8_t,640> sat{};std::array<int,80> teams{};unsigned serial=0; };
std::deque<FormationUpload> formation_queue;
std::array<int,80> formation_building{},formation_displayed{};
std::array<uint32_t,1024> formation_row{};
bool formation_active=false;
int formation_line=-1;
unsigned formation_serial=0;
struct Piece { int sat,x,y,w,h,tile,pal,flip,group; std::array<uint32_t,1024> pixels{}; };
struct Group { double world=0,road_x=0,x=0,y=0,w=0,h=0,z=0,from_x=0,from_foot=0,scale=1,zoom=1,travel=0,roll=0,steer=0,spark=0,ground_clip=224; unsigned signature=0; int id=-1,angle=-1,team=-1,wheel=0,passing_side=0,palette=0,tree=-1,side=0; bool source=false,full=false,architecture=false,portal=false,culled=false,flagman=false; };
// GP II finish-line flag marshal (object DB80, routine 14986), largest native
// pose decoded live from VRAM/CRAM each frame it is drawn.
struct FlagmanArt{int width=0,height=0,left=0,top=0;std::vector<uint32_t> pixels;} flagman_art;
constexpr unsigned gp2_flagman_object=0xdb80,gp2_flagman_routine=0x14986,gp2_flagman_pose=0x3306;
struct ObjectPose { unsigned object,routine,mapping; int x,y,tile,depth; unsigned family=0,stamp=0; double gap=0,lateral=0,dgap=0,dlateral=0,distance=0,phase=0,zsmooth=0; bool architecture=false; };
std::vector<ObjectPose> pending_objects,world_objects;
std::array<double,120> world_center{},pending_center{};
std::array<double,120> object_center{};bool object_center_ready=false;
std::array<double,96> world_ground{},pending_ground{};
std::array<double,120> center_velocity{};
std::array<double,120> presentation_center{};
std::array<bool,120> presentation_ray_valid{};
bool precise_center=false,precise_ground=false;
std::array<double,96> presentation_ground{};
double observed_lateral=0,lateral_velocity=0,lateral_from=0;unsigned lateral_interval=3; // ticks between native FC44 commits (GP II ~20 Hz)
unsigned lateral_stamp=0;
bool lateral_valid=false;
std::array<double,96> ground_velocity{},stripe_distance{};
double camera_phase=0,camera_velocity=0,pending_phase=0;
double panorama_scroll=0,panorama_velocity=0,pending_panorama=0;
unsigned camera_stamp=0;
unsigned camera_measured_at=0,pending_measured_at=0;
unsigned pending_stamp=0;
unsigned building_stamp=0;
double observed_phase=0,observed_velocity=0;
struct PhaseSample{unsigned tick;double raw,unwrapped,vt;};std::deque<PhaseSample> phase_samples;
unsigned observed_stamp=0;
bool observed_valid=false;
unsigned build_serial,committed_serial,world_serial,assembled_serial;
bool objects_ready;
struct UploadedScene {
 unsigned serial=0,sat_base=0,stamp=0,measured_at=0;
 std::array<uint8_t,640> sat{};
 std::array<unsigned,80> owner{};
 std::array<unsigned,80> mirror_owner{};
 std::vector<ObjectPose> objects;
 std::array<double,120> center{};
 std::array<double,96> ground{};
 double phase=0,panorama=0;
};
std::deque<UploadedScene> scene_queue;
std::deque<UploadedScene> motion_queue;
std::vector<ObjectPose> removal_objects;
double removal_phase=0;
std::array<unsigned,80> building_owner{},displayed_owner{};
std::array<unsigned,80> building_mirror_owner{},displayed_mirror_owner{};
bool upload_matched=false;
unsigned original_world_pixels=0,leaked_world_pixels=0,unsupported_slots=0;
void publish_scene(const UploadedScene&);
std::vector<Piece> pieces;
std::vector<Group> groups,history;
std::array<bool,80> selected{};
std::vector<uint32_t> foreground,composed,native_frame;
std::vector<uint32_t> gpu_pixels,road_pixels;
std::vector<uint8_t> crisp_interior;
std::vector<uint32_t> player_underlay;
std::vector<uint8_t> tunnel_occlusion;
std::vector<uint32_t> portal_surface;
struct RoadRow { bool valid=false,road=false; double road_q=-1,road_center=0; int road_bank=112; };
std::array<RoadRow,224> road_rows;
SDL_Texture *target=nullptr,*background=nullptr,*sprite=nullptr,*front=nullptr,*road=nullptr;
SDL_Texture *scenery_target=nullptr,*scenery_mask=nullptr;
std::vector<uint32_t> scenery_alpha;
constexpr int coverage_scale=3;
int texture_width=0,texture_height=0,game_index,native_width,offset_width;
unsigned tick,prepared_tick=~0u;
bool active,prepared,smooth_zoom;
bool renderer_ready=true;
uint64_t compose_key=~0ull;
unsigned draws,tracked;
unsigned player_slots=0;
unsigned player_window_pixels=0;
unsigned mirror_team_pixels=0;
std::vector<uint32_t> layer_native,layer_b;
std::array<unsigned,224> layer_diff{};
struct ArcadeArt { int width=0,height=0;std::vector<uint32_t> pixels;std::array<std::vector<uint32_t>,4> rolling; };
struct Gp2CarSource {ArcadeArt art;int x=0,y=0;std::vector<uint8_t> ink;std::array<uint32_t,64> palette{};};
std::array<std::vector<Gp2CarSource>,5> gp2_cars;
std::array<ArcadeArt,6> arcade_trees;std::array<double,6> tree_roots{};
// X-board roadside replacements (props21): chevron boards and tyre stacks.
// unit = world size of one source pixel, in rival-car source pixels at the
// same depth, measured in MAME (tools/extract_arcade_props21.py).
struct ArcadeProp { ArcadeArt art; double unit=0; };
enum { PROP_CHEVRON_BLUE, PROP_CHEVRON_RED, PROP_TYRES_RED, PROP_TYRES_GREEN, PROP_COUNT };
std::array<ArcadeProp,PROP_COUNT> arcade_props21;
std::map<unsigned,uint32_t> prop_accent;
// Sprite sweep (tools/sprite_sweep21.py, assets/arcade-smgp/sweep21/map.txt):
// cartridge roadside mappings with an arcade equivalent. kind 1 = chevron board,
// 2 = tyre stack (measured props21 path), 3 = other art (equal-width swap).
// Key: signature | game<<31.
struct SweepProp { int kind=0; bool flip=false; ArcadeArt art; };
std::map<unsigned,SweepProp> sweep_props;
struct PlayerArt { ArcadeArt image;int dx=0,y=0; };
std::map<std::string,PlayerArt> arcade_player;
std::array<std::map<std::string,PlayerArt>,7> full_player;
struct WheelOrigin {int dx=0,y=0;bool valid=false;};
std::array<std::array<WheelOrigin,2>,7> full_wheel_origins;
std::array<int,8> full_selector{};
std::array<std::array<std::map<std::string,WheelOrigin>,7>,3> full_geometry;
bool full_player_valid=false;
struct AuthoredPose {int left=7,right=7,hands=0,dx=0,y=0,flip=0,left_shift=0,right_shift=0;bool valid=false;};
std::array<AuthoredPose,19> authored_poses;
std::array<int,16> authored_vertical{};
std::vector<uint8_t> player_glass;
bool authored_valid=false;
struct PaintProbe { int width=0,height=0;std::vector<uint8_t> roles; };
std::array<PaintProbe,7> mirror_glass;
std::array<ArcadeArt,5> arcade_cars;
std::array<ArcadeArt,2> arcade_slight,arcade_sparks;
struct EffectFrame {ArcadeArt image;double scale=0,dx=0,dy=0;};
std::vector<EffectFrame> spark_cycle;
struct EffectClock {unsigned routine=0,stamp=0,last_start=0;int age=-1,curve=0;bool kerb=false;};
// Spark cadence (owner 0.21ac): last burst on screen, and the tick the player
// left a standstill of 2+ s (grid start: full tanks, first 30 s spark more).
unsigned spark_global_tick=0,spark_start_tick=0,spark_still_since=0;bool spark_still=false;
// Last tick of the GP II tunnel approach (phase 1); the facade still reaches
// the edges for ~24 ticks into phase 2.
unsigned facade_tick=0;
std::map<unsigned,EffectClock> effect_clocks;
std::array<PaintProbe,5> car_paint;
std::map<unsigned,ArcadeArt> arcade_props;
std::map<uint64_t,std::array<uint8_t,1024>> prop_source_tiles;
bool arcade_loaded=false;
struct PlantedTree {double distance,lateral;int art;bool settled=false;unsigned hit_at=0;};
std::vector<PlantedTree> planted_trees;
unsigned tree_track_length=0,tree_seed=0;
ArcadeArt ending_banner,shared_gameover,shared_retire;std::vector<uint32_t> ending_background;int ending_width=0;bool ending_active=false;
// Native (H32) top-left of the letters at the instant the zoom takes over; the zoom starts there at 1:1.
int ending_anchor_x=28,ending_anchor_y=146;
bool result_retire=false,ending_is_retire=false;
std::vector<uint32_t> last_race_background;int last_race_width=0;
unsigned ending_age=0;
struct RenderClock {double phase=0,velocity=0,lateral=0,lateral_velocity=0,panorama=0,panorama_speed=0;unsigned length=0;bool valid=false;} render_clock;
double observed_panorama=0,observed_panorama_speed=0;
unsigned panorama_stamp=0;
bool panorama_valid=false;
struct ChassisState {
 double distance=0,speed=0,lateral=0,lateral_speed=0;
 double slip=0;
 int wheel=0,plane=0,contact=2,tween=0,yaw=0,bob=0,pending_plane=0;
 bool kerb_edge=false; // GP II kerb contact with hysteresis (see contact selection)
 unsigned clock=0,stamp=0;
 bool valid=false,geometry_valid=false;
 std::array<double,8> geometry{},from_geometry{},target_geometry{};
} chassis;
void save_rgba(const std::filesystem::path&path,const std::vector<uint32_t>&pixels,unsigned w,unsigned h){
 std::ofstream f(path,std::ios::binary);if(!f)return;
 f.write("SGPA",4);f.write((char*)&w,4);f.write((char*)&h,4);
 for(auto p:pixels){char rgba[4]={char(p>>16),char(p>>8),char(p),char(p>>24)};f.write(rgba,4);}
}
struct FilteredArt {int w,h;std::vector<uint32_t> pixels;};
uint32_t area_colour(const std::array<uint32_t,4>&p){
 unsigned a=0,r=0,g=0,b=0;
 for(auto c:p){unsigned alpha=c>>24;a+=alpha;r+=(c>>16&255)*alpha;g+=(c>>8&255)*alpha;b+=(c&255)*alpha;}
 return a?((a/4)<<24)|((r/a)<<16)|((g/a)<<8)|(b/a):0;
}
// Filter in source space before the fractional GPU transform. The former
// three nearest samples per destination pixel alias distant tyre bands,
// posts and sign letters as scale changes. Premultiplied area mipmaps keep
// silhouettes and a continuous LOD; no image-flow or guest-frame blending.
FilteredArt minify(const std::vector<uint32_t>&pixels,int w,int h,double scale){
 FilteredArt a{w,h,pixels};
 if(scale>=1||scale<=0||getenv("SMGP_UNFILTERED_SOURCES"))return a;
 double lod=std::max(0.0,std::log2(1/scale));int level=(int)lod;double fraction=lod-level;
 auto half=[](const FilteredArt&src){
  FilteredArt dst{std::max(1,(src.w+1)/2),std::max(1,(src.h+1)/2),{}};dst.pixels.resize(dst.w*dst.h);
  for(int y=0;y<dst.h;y++)for(int x=0;x<dst.w;x++){
   std::array<uint32_t,4> p;for(int yy=0;yy<2;yy++)for(int xx=0;xx<2;xx++)p[yy*2+xx]=src.pixels[std::min(y*2+yy,src.h-1)*src.w+std::min(x*2+xx,src.w-1)];
   dst.pixels[y*dst.w+x]=area_colour(p);
  }return dst;
 };
 for(int n=0;n<level&&(a.w>1||a.h>1);n++)a=half(a);
 if(fraction>.001&&(a.w>1||a.h>1)){
  auto coarse=half(a);
  for(int y=0;y<a.h;y++)for(int x=0;x<a.w;x++){
   double sx=(x+.5)*coarse.w/a.w-.5,sy=(y+.5)*coarse.h/a.h-.5;int ix=(int)std::floor(sx),iy=(int)std::floor(sy);
   double fx=sx-ix,fy=sy-iy,alpha=0,red=0,green=0,blue=0;
   for(int yy=0;yy<2;yy++)for(int xx=0;xx<2;xx++){
    auto p=coarse.pixels[std::clamp(iy+yy,0,coarse.h-1)*coarse.w+std::clamp(ix+xx,0,coarse.w-1)];
    double weight=(xx?fx:1-fx)*(yy?fy:1-fy)*(p>>24);alpha+=weight;red+=weight*(p>>16&255);green+=weight*(p>>8&255);blue+=weight*(p&255);
   }
   auto p=a.pixels[y*a.w+x];double pa=(p>>24)*(1-fraction),ca=alpha*fraction,total=pa+ca;
   if(total>0)a.pixels[y*a.w+x]=(uint32_t(total)<<24)|(uint32_t(((p>>16&255)*pa+red*fraction)/total)<<16)|(uint32_t(((p>>8&255)*pa+green*fraction)/total)<<8)|uint32_t(((p&255)*pa+blue*fraction)/total);
   else a.pixels[y*a.w+x]=0;
  }
 }
 return a;
}
void tire_frames(ArcadeArt&a){
 // Exact original rear/GP-II drawings. The X-board captures do not contain
 // the synthetic front-to-rear texel ranking used by 0.8/0.9.
 for(auto&phase:a.rolling)phase=a.pixels;
}
void load_arcade(){
 if(arcade_loaded)return;arcade_loaded=true;
 const char*option=getenv("SMGP_ARCADE");if(option&&option[0]=='0')return;
 char*base=SDL_GetBasePath();std::filesystem::path folder=base?base:".";SDL_free(base);
 for(int i=0;i<7&&!std::filesystem::exists(folder/"assets/arcade-smgp/rear-straight.rgba");++i)folder=folder.parent_path();
 folder/="assets/arcade-smgp";
 std::ifstream gp2_layout(folder/"gp2-cars12/layout.txt");int gp2_angle;std::string gp2_file;
 while(gp2_layout>>gp2_angle>>gp2_file)if(gp2_angle>=0&&gp2_angle<5){
  std::ifstream f(folder/"gp2-cars12"/gp2_file,std::ios::binary);char magic[4];int w=0,h=0;Gp2CarSource s;
  f.read(magic,4);f.read((char*)&w,4);f.read((char*)&h,4);f.read((char*)&s.x,4);f.read((char*)&s.y,4);
  if(!f||std::string(magic,4)!="SGPI"||w<1||w>512||h<1||h>256)continue;
  f.read((char*)s.palette.data(),256);s.ink.resize(w*h);f.read((char*)s.ink.data(),s.ink.size());if(!f)continue;
  s.art.width=w;s.art.height=h;s.art.pixels.resize(w*h);
  for(int i=0;i<w*h;i++)s.art.pixels[i]=s.ink[i]<64?s.palette[s.ink[i]]:0;
  gp2_cars[gp2_angle].push_back(std::move(s));
 }
 const char*names[]={"rear-sharp-mirror","rear-turn-mirror","rear-straight","rear-turn","rear-sharp"};
 auto read=[&](const std::string&name,ArcadeArt&a){
  std::ifstream f(folder/(name+".rgba"),std::ios::binary);
  char magic[4]{};uint32_t w=0,h=0;f.read(magic,4);f.read((char*)&w,4);f.read((char*)&h,4);
  if(!f||std::string(magic,4)!="SGPA"||!w||!h||w>512||h>256)return;
  std::vector<uint8_t> rgba((size_t)w*h*4);f.read((char*)rgba.data(),rgba.size());if(!f)return;
  a.width=w;a.height=h;a.pixels.resize((size_t)w*h);
  for(size_t n=0;n<a.pixels.size();n++)a.pixels[n]=((uint32_t)rgba[n*4+3]<<24)|((uint32_t)rgba[n*4]<<16)|((uint32_t)rgba[n*4+1]<<8)|rgba[n*4+2];
 };
 for(int i=0;i<5;i++){
  read(names[i],arcade_cars[i]);
 }
 std::ifstream layout(folder/"player/layout.txt");std::string name;int dx,y;
 while(layout>>name>>dx>>y){PlayerArt p;read("player/"+name,p.image);p.dx=dx;p.y=y;if(p.image.width)arcade_player[name]=std::move(p);}
 std::ifstream poses(folder/"player-authored09/poses.txt");int yaw;
 while(poses>>yaw>>dx>>y){AuthoredPose p;p.left=dx;p.right=y;if(!(poses>>p.hands>>p.dx>>p.y>>p.flip>>p.left_shift>>p.right_shift))break;if(yaw>=-9&&yaw<=9){p.valid=true;authored_poses[yaw+9]=p;}}
 std::ifstream vertical(folder/"player-authored09/vertical.txt");for(auto&v:authored_vertical)vertical>>v;
 std::ifstream glass(folder/"player-authored09/glass.mask",std::ios::binary);char magic[4]{};uint32_t gw=0,gh=0;glass.read(magic,4);glass.read((char*)&gw,4);glass.read((char*)&gh,4);
 if(glass&&std::string(magic,4)=="SGPM"&&gw==146&&gh==43){player_glass.resize(gw*gh);glass.read((char*)player_glass.data(),player_glass.size());}
 authored_valid=vertical&&glass&&std::all_of(authored_poses.begin(),authored_poses.end(),[](const AuthoredPose&p){return p.valid;});
 std::ifstream full_layout(folder/"player-full10/layout.txt");int plane;std::string part,asset;
 while(full_layout>>plane>>part>>asset>>dx>>y)if(plane>=-3&&plane<=3){
  PlayerArt p;read("player-full10/"+asset,p.image);p.dx=dx;p.y=y;
  if(p.image.width)full_player[plane+3][part]=std::move(p);
 }
 std::ifstream origins(folder/"player-full10/wheel-origins.txt");char side;
 while(origins>>plane>>side>>dx>>y)if(plane>=-3&&plane<=3&&(side=='L'||side=='R'))full_wheel_origins[plane+3][side=='R']={dx,y,true};
 std::ifstream selector(folder/"player-full10/selector.txt");for(auto&value:full_selector)selector>>value;
 std::ifstream geometry(folder/"player-full10/geometry.txt");int contact;
 while(geometry>>contact>>plane>>part>>dx>>y)if(contact>=0&&contact<3&&plane>=-3&&plane<=3)full_geometry[contact][plane+3][part]={dx,y,true};
 full_player_valid=std::all_of(full_player.begin(),full_player.end(),[](const auto&p){return p.size()==4;})&&
  std::all_of(full_wheel_origins.begin(),full_wheel_origins.end(),[](const auto&p){return p[0].valid&&p[1].valid;})&&selector&&full_geometry[2][3].size()==4;
 for(int plane=-3;plane<=3;plane++){
  std::ifstream f(folder/"mirrors11"/("plane"+std::to_string(plane)+".mask"),std::ios::binary);
  char magic[4]{};uint32_t w=0,h=0;f.read(magic,4);f.read((char*)&w,4);f.read((char*)&h,4);
  const auto&art=full_player[plane+3]["fairing"].image;
  if(!f||std::string(magic,4)!="SGPM"||w!=art.width||h!=art.height||!w||!h)continue;
  std::vector<uint8_t> mask(size_t(w)*h);f.read((char*)mask.data(),mask.size());
  if(f&&std::all_of(mask.begin(),mask.end(),[](uint8_t n){return n<=1;})&&std::count(mask.begin(),mask.end(),1)>=40)mirror_glass[plane+3]={int(w),int(h),std::move(mask)};
 }
 for(int i=0;i<5;i++){
  tire_frames(arcade_cars[i]);
  if(const char*dir=getenv("SMGP_WHEEL_EXPORT_DIR"))for(int phase=0;phase<4;phase++){
   const auto&a=arcade_cars[i];save_rgba(std::filesystem::path(dir)/("angle"+std::to_string(i)+"-phase"+std::to_string(phase)+".rgba"),a.rolling[phase],a.width,a.height);
  }
 }
 read("rear-slight-mirror",arcade_slight[0]);read("rear-slight",arcade_slight[1]);
 for(auto&a:arcade_slight)tire_frames(a);
 read("animation/sparks-small",arcade_sparks[0]);read("animation/sparks-large",arcade_sparks[1]);
 std::ifstream cycle(folder/"effects10/cycle.txt");double zoom,ex,ey;
 while(cycle>>name>>zoom>>ex>>ey){EffectFrame f;f.scale=zoom;f.dx=ex;f.dy=ey;if(name!="none")read("effects10/"+name,f.image);spark_cycle.push_back(std::move(f));}
 for(int i=0;i<5;i++){
  std::ifstream f(folder/"livery"/("angle"+std::to_string(i)+".mask"),std::ios::binary);
  char magic[4]{};uint32_t w=0,h=0;f.read(magic,4);f.read((char*)&w,4);f.read((char*)&h,4);
  if(!f||std::string(magic,4)!="SGPM"||!w||!h||w>512||h>256)continue;
  std::vector<uint8_t> roles((size_t)w*h);f.read((char*)roles.data(),roles.size());
  if(!f||std::any_of(roles.begin(),roles.end(),[](uint8_t n){return n>4;}))continue;
  car_paint[i]={int(w),int(h),std::move(roles)};
 }
 {
  std::ifstream units(folder/"props21/units.txt");std::string prop;double unit,elevation;
  const char*prop_names[PROP_COUNT]={"chevron-blue","chevron-red","tyres-red-full","tyres-green-full"};
  while(units>>prop>>unit>>elevation)for(int i=0;i<PROP_COUNT;i++)if(prop==prop_names[i]&&unit>0&&unit<8){read("props21/"+prop,arcade_props21[i].art);arcade_props21[i].unit=unit;}
 }
 {
  std::ifstream sweep(folder/"sweep21/map.txt");int sweep_game,flip;std::string signature,kind,file;
  while(sweep>>sweep_game>>signature>>kind>>file>>flip){
   if(sweep_game<1||sweep_game>2)continue;SweepProp p;p.flip=flip!=0;
   p.kind=kind=="chevron"?1:kind=="tyres"?2:kind=="art"?3:0;if(!p.kind)continue;
   if(p.kind==3){read("sweep21/"+file,p.art);if(!p.art.width)continue;}
   sweep_props[unsigned(std::stoul(signature,nullptr,16))|(unsigned(sweep_game-1)<<31)]=std::move(p);
  }
 }
 // Without the measured props21 set, the older equal-width tyre replacement remains.
 if(!arcade_props21[PROP_TYRES_RED].art.width){read("tyres-red",arcade_props[0x11452]);read("tyres-green",arcade_props[0x114ee]);}
 read("gameover13/game-over",shared_gameover);
 read("retire17/retire",shared_retire);
 for(int n=0;n<6;n++){read("trees13/tree"+std::to_string(n),arcade_trees[n]);tree_roots[n]=arcade_trees[n].width*.5;}
 std::ifstream roots(folder/"trees13/roots.txt");int root_id;double root_x;while(roots>>root_id>>root_x)if(root_id>=0&&root_id<6)tree_roots[root_id]=root_x;
 read("billboard-longines-campari",arcade_props[0x11584]);read("billboard-longines-campari",arcade_props[0x11a34]);
 if(arcade_cars[2].width)fprintf(stderr,"[SMGP ARCADE] original X-board car sources loaded, independent of guest VRAM\n");
}
bool native_paint(const std::vector<uint32_t>&pixels,int w,int h,int angle,std::array<uint32_t,4>&paint){
 const auto&probe=car_paint[angle];if(w!=probe.width||h!=probe.height)return false;
 std::array<std::map<uint32_t,unsigned>,4> counts;
 for(size_t n=0;n<probe.roles.size();n++)if(probe.roles[n]&&(pixels[n]>>24))++counts[probe.roles[n]-1][pixels[n]];
 for(int role=0;role<4;role++){
  if(counts[role].empty())return false;
  paint[role]=std::max_element(counts[role].begin(),counts[role].end(),[](auto&a,auto&b){return a.second<b.second;})->first;
 }
 return true;
}
uint32_t paint_mix(uint32_t a,uint32_t b,unsigned n,unsigned d=255){
 uint32_t value=0xff000000u;
 for(int shift:{0,8,16})value|=((((a>>shift)&255)*(d-n)+((b>>shift)&255)*n)/d)<<shift;
 return value;
}
void colour_arcade(ArcadeArt const&art,std::vector<uint32_t>&pixels,const std::array<uint32_t,4>&paint,unsigned white,int phase=0){
 pixels=art.rolling[phase&3].empty()?art.pixels:art.rolling[phase&3];
 for(size_t n=0;n<pixels.size();n++){
  auto&pixel=pixels[n];if(!(pixel>>24))continue;
  switch(pixel&0xffffffu){
   case 0xef4231:pixel=paint[2];break;
   case 0x943119:pixel=paint[3];break;
   case 0xf7735b:pixel=paint_mix(paint[2],0xff000000u|white|(white<<8)|(white<<16),32);break;
   case 0x520000:pixel=paint_mix(0xff000000u,paint[3],141);break;
   case 0xe6e6e6:pixel=paint[0];break;
   case 0xd6cece:pixel=paint_mix(paint[1],paint[0],216);break;
   case 0xa4a4a4:if(n/art.width<(size_t)(art.height*.27))pixel=paint[1];else pixel=paint_mix(0xff000000u,pixel,white);break;
   default:pixel=paint_mix(0xff000000u,pixel,white);break;
  }
 }
}
unsigned word(const GVDP *v,unsigned a){return (v->vram[a&65535]<<8)|v->vram[(a+1)&65535];}
struct GameProfile{
 unsigned player,cars,cars_end,props,props_end,stride,curve,center,ground,length,pause,orient,panomap,stripes,special;
};
const GameProfile profiles[]={
 {0xae00,0xb080,0xb440,0xb840,0xc080,0x40,0x9500,0x9600,0x9700,0x9206,0xfc66,0x9c00,0xc080,0x6f940,0x923c},
 {0xd9a0,0xdd10,0xe1c0,0xeb70,0xf5c0,0x50,0xc380,0xc480,0xc580,0xfc48,0xfe42,0x14a80,0x1e00,0x230da,0xfc86}
};
const GameProfile& profile(){return profiles[game_index];}
unsigned rom_limit(){return game_index?0x100000:0x80000;}
unsigned rw(unsigned a){a&=65535;return (g_ram[a]<<8)|g_ram[(a+1)&65535];}
unsigned rl(unsigned a){return (rw(a)<<16)|rw(a+2);}
bool flagman_option(){const char*o=getenv("SMGP_FLAGMAN");return !(o&&o[0]=='0');}
// Flag marshal: GP II object DB80 routine 14986 (family 9E8); World object
// AF80 routine A1AA (family 108FC). Same 9-pose ladder in both cartridges.
unsigned flagman_object(){return game_index?gp2_flagman_object:0xaf80;}
bool flagman_actor(unsigned a){return smooth_zoom&&flagman_option()&&(a&65535)==flagman_object()&&rl(a)==(game_index?gp2_flagman_routine:0xa1aau);}
bool championship(){return game_index?(!rw(0xfc02)&&!rw(0xfc08)&&rw(0xc088)<16):rw(0x9140)==1;}
bool formation_page(){
 if(!championship())return false;
 unsigned page=rl(game_index?0xff20:0xff0c);
 if(game_index&&page==0xd34e)page=rl(0xff24);
 if(!game_index&&(page==0x3d8||page==0x3e0))page=rl(0xff10);
 return game_index?page==0x1167e:page==0x429e;
}
int team_for_grid_rank(unsigned rank){
 if(rank>=16)return -1;
 int player=game_index?rw(0xc088):g_ram[0x9043]&15;
 int rival=game_index?rw(0xc08a):g_ram[0x9042]&15;
 unsigned player_rank=rw(game_index?0xff56:0xff30),rival_rank=rw(game_index?0xff60:0xff3a);
 bool has_rival=rw(game_index?0xfc1e:0xff4e)&&rival!=player&&rival<16&&rival_rank<16;
 if(rank==player_rank)return player;
 if(has_rival&&rank==rival_rank)return rival;
 int ordinal=int(rank)-(rank>player_rank)-(has_rival&&rank>rival_rank);
 for(int team=0;team<16;team++)if(team!=player&&(!has_rival||team!=rival))if(ordinal--==0)return team;
 return -1;
}
int formation_object_team(unsigned object){
 if(!game_index||!formation_page()||object<0xd900||object>=0xde00||(object-0xd900)%80)return -1;
 unsigned map=rl(object+4),routine=rl(object);
 if((map!=0x348a6&&map!=0x348ae&&map!=0x348b6)||routine<0x117c8||routine>0x119c6)return -1;
 return team_for_grid_rank(rw(object+14));
}
int team_for_actor(unsigned a){
 if(!championship())return -1;
 int player=game_index?rw(0xc088):g_ram[0x9043]&15,rival=game_index?rw(0xc08a):g_ram[0x9042]&15;
 bool has_rival=(game_index?rw(0xfc1e):rw(0xff4e))!=0&&rival!=player&&rival<16;
 if(a==profile().player||a==profile().player+profile().stride)return player;
 const auto&pr=profile();if(a<pr.cars||a>=pr.cars_end)return -1;
 int slot=(a-pr.cars)/pr.stride;
 if(has_rival){
  if(game_index){if(rw(a+12))return rival;for(unsigned prior=pr.cars;prior<a;prior+=pr.stride)if(rw(prior+12))--slot;}
  else{if(!slot)return rival;--slot;}
 }
 for(int team=0;team<16;team++)if(team!=player&&(!has_rival||team!=rival)){
  if(slot--==0)return team;
 }
 return -1;
}
std::array<uint32_t,4> team_paint(int team,unsigned fade=255,int level=GENESIS_DAC_NORMAL){
 std::array<uint32_t,4> p{};if(team<0||team>15)return p;
 unsigned at=game_index?0x4d30+team*8:0x20fae+team*56+10;
 unsigned words[4]={unsigned(g_rom[at+4]<<8|g_rom[at+5]),unsigned(g_rom[at+6]<<8|g_rom[at+7]),unsigned(g_rom[at]<<8|g_rom[at+1]),unsigned(g_rom[at+2]<<8|g_rom[at+3])};
 for(int i=0;i<4;i++)p[i]=paint_mix(0xff000000u,genesis_dac_cram_to_argb(words[i],level),fade);
 return p;
}
bool player_arcade(){return active&&smooth_zoom&&rl(profile().player+4)==(game_index?0xa14:0x129ae)&&arcade_player.size()==148&&authored_valid&&full_player_valid;}
bool player_actor(unsigned a){return a==profile().player||a==profile().player+profile().stride;}
bool player_tile(unsigned tile){return game_index?((tile>=0x5c0&&tile<=0x5d7)||(tile>=0x550&&tile<=0x55c)):(tile>=0x46d&&tile<=0x491);}
bool player_hand_pattern(unsigned tile,int palette){return palette==0&&(game_index?(tile>=0x10&&tile<=0x1b):(tile>=0x461&&tile<=0x46c));}
unsigned scene_fade(const GVDP*v){
 unsigned white=0;for(unsigned i=0;i<64;i++){uint32_t p=genesis_dac_cram_to_argb(v->cram[i],GENESIS_DAC_NORMAL);unsigned r=p>>16&255,g=p>>8&255,b=p&255;if(r==g&&g==b)white=std::max(white,r);}return white;
}
double present_phase();
double phase_difference(double,double);
// Learned logic-step period (host ticks) and the fixed display delay derived from
// it. The presented camera/lateral clocks interpolate between commits that have
// already happened, this many ticks behind the native game.
double phase_period=3;
// Display delay shared by the phase clock, the lateral clock and camera_age().
// Half a tick over one logic step: a commit observed one VBlank late is then
// still interpolated instead of being held (see regular_commit_time).
double presentation_delay(){return phase_period+.5;}
// Virtual arrival time of a native commit. Measured in the owner's session
// (telemetry-smgp2-20261111-224251.csv, 1761 moving commits): 1702 arrive 3 ticks
// after the previous one, 30 arrive after 4 and 29 after 2, and the distance per
// commit is the same in all three cases (0.0265 x speed). The 2/4 gaps are only
// the VBlank at which the host happens to observe the write, not game time.
// Tracking them (period gain .15, 30% pull to the real tick) changed the display
// delay and the segment length at every such commit: the presented speed fell
// ~25% for one tick and recovered, about once per 1.5 s, on the road and on every
// roadside object. Commits are therefore placed one LEARNED period apart; the
// period follows a real cadence change only when it repeats, and the timeline is
// pulled very gently to the real ticks so it cannot drift away from them.
struct CommitCadence{double run_value=0;int run=0;void clear(){run_value=0;run=0;}};
double regular_commit_time(unsigned tick,double dt,double last_vt,double&period,CommitCadence&cadence){
 // A value that did not change for one logic step skips a commit: count the
 // steps actually spanned so the timeline stays in step.
 double steps=std::max(1.0,std::round(dt/period)),per=dt/steps;
 if(per==cadence.run_value)cadence.run++;else{cadence.run_value=per;cadence.run=1;}
 if(cadence.run>=3&&std::abs(per-period)>.5)period=per;
 else period+=(std::clamp(per,1.5,5.0)-period)*.02;
 double expected=last_vt+period*steps;
 double vt=std::clamp(expected+(double(tick)-expected)*.05,double(tick)-1.5,double(tick)+1.0);
 return std::max(vt,last_vt+.5);
}
CommitCadence phase_cadence;
double camera_age(){
 if(rw(profile().pause))return 0;
 // Transport the measured snapshot to the PRESENTED instant on the same delayed
 // timeline as the camera clock, in elapsed host ticks. The previous definition
 // divided the phase difference by the camera speed, so it was zero whenever the
 // camera did not move (race start, standstill) and the other cars then advanced
 // in 20 Hz steps; it also inherited every irregularity of the phase clock.
 return std::clamp(double(tick)-presentation_delay()-double(camera_stamp),-6.0,9.0);
}
double phase_difference(double a,double b){double d=a-b,length=rw(profile().length);if(length>100){while(d>length*.5)d-=length;while(d<-length*.5)d+=length;}return d;}
// Native phase is committed in steps (every 2-3 VBlanks). Presenting each step
// directly makes the whole road and every roadside object lurch on that cadence.
// See the delayed interpolation below.
// The native game commits its phase once per logic step (nominally every 3
// VBlanks, but the host sees 2/3/4-tick gaps while the step itself always
// advances one fixed unit of game time). Forecasting the next commit made every
// deceleration in a bend overshoot and then stall (measured: 0.82, 0.09, 0.09,
// 0.09, 1.06 per tick), and the roadside objects inherited that stutter amplified
// by the road curvature. The clock now only INTERPOLATES between commits that
// already happened, a fixed small delay behind them, on a regularised timeline:
// each commit gets a virtual time one learned period after the previous one,
// pulled only 30% toward its real arrival tick, so 2/3/4-tick arrival jitter does
// not become speed jitter. No forecast, therefore no overshoot, no stall.
void push_phase_sample(unsigned tick,double current){
 if(phase_samples.empty()){phase_period=3;phase_cadence.clear();phase_samples.push_back({tick,current,current,double(tick)});return;}
 const auto last=phase_samples.back();
 double unwrapped=last.unwrapped+phase_difference(current,last.raw);
 // A commit never moves the presented clock backwards.
 unwrapped=std::max(unwrapped,last.unwrapped);
 if(last.tick==tick){phase_samples.back()={tick,current,unwrapped,last.vt};return;}
 double dt=double(tick-last.tick),vt;
 if(dt>8){phase_period=3;phase_cadence.clear();vt=double(tick);}
 else vt=regular_commit_time(tick,dt,last.vt,phase_period,phase_cadence);
 // After a pause in commits (standstill, or a value that simply did not change)
 // the previous value is held until one regular period before this commit, so the
 // motion ramps over ONE period instead of creeping across the whole silence.
 if(dt>phase_period+1.5&&vt-phase_period>last.vt+.25)phase_samples.push_back({last.tick,last.raw,last.unwrapped,vt-phase_period});
 phase_samples.push_back({tick,current,unwrapped,vt});
 while(phase_samples.size()>8)phase_samples.pop_front();
}
bool phase_fit(unsigned tick,double&predicted,bool allow_hold_extrapolation){
 if(phase_samples.size()<2)return false;
 double T=double(tick)-presentation_delay();
 const auto&first=phase_samples.front();
 if(T<=first.vt){predicted=first.unwrapped;return true;}
 for(size_t i=1;i<phase_samples.size();i++){
  const auto&a=phase_samples[i-1];const auto&b=phase_samples[i];
  if(T<=b.vt){double span=std::max(.25,b.vt-a.vt);predicted=a.unwrapped+(b.unwrapped-a.unwrapped)*std::clamp((T-a.vt)/span,0.0,1.0);return true;}
 }
 const auto&a=phase_samples[phase_samples.size()-2];const auto&b=phase_samples.back();
 double slope=std::clamp((b.unwrapped-a.unwrapped)/std::max(.25,b.vt-a.vt),0.0,3.0);
 predicted=b.unwrapped+(allow_hold_extrapolation?slope*std::min(T-b.vt,1.0):0.0);
 return true;
}// Same delayed, regularised interpolation for the lateral camera offset. It is
// committed at the same logic step as the phase; showing each commit as a
// constant-speed segment that ends "when the next one is due" held the camera for
// a tick whenever a commit arrived one tick late (-8,-8,0 and 18.7,18.7,18.7,0 per
// tick), and the lateral term of every roadside object is magnified by q.
struct DelayedLateral{
 struct S{unsigned tick;double value,vt;};std::deque<S> s;double period=3;CommitCadence cadence;
 void clear(){s.clear();period=3;cadence.clear();}
 void push(unsigned tick,double value){
  if(s.empty()){period=3;cadence.clear();s.push_back({tick,value,double(tick)});return;}
  const auto last=s.back();
  if(last.tick==tick){s.back().value=value;return;}
  double dt=double(tick-last.tick),vt;
  if(dt>8){period=3;cadence.clear();vt=double(tick);}
  else vt=regular_commit_time(tick,dt,last.vt,period,cadence);
  // Hold the previous value through a silence, then ramp over one period. FC44 /
  // 9204 also skips single commits while the car keeps sliding the same way
  // (measured 9,9,9,0,9 per step; 99 of 1726 commits in the owner's session).
  // Holding there stopped the camera for three ticks in the middle of a drift
  // and every near roadside object stepped sideways and back; when the motion
  // continues in the same direction the skipped step is crossed at constant
  // speed instead. A stop, a start or a reversal still holds, so the camera
  // never creeps through a real standstill nor moves away from its target.
  if(dt>period+1.5&&vt-period>last.vt+.25){
   bool sliding=false;
   if(s.size()>=2&&dt<=2.5*period+.5){
    const auto&before=s[s.size()-2];
    double earlier=(last.value-before.value)/std::max(.25,last.vt-before.vt),now=value-last.value;
    sliding=earlier*now>0&&std::abs(earlier)>.05;
   }
   if(!sliding)s.push_back({last.tick,last.value,vt-period});
  }
  s.push_back({tick,value,vt});while(s.size()>8)s.pop_front();
 }
 bool eval(unsigned tick,double delay,double&out)const{
  if(s.size()<2)return false;
  double T=double(tick)-delay;
  if(T<=s.front().vt){out=s.front().value;return true;}
  for(size_t i=1;i<s.size();i++){const auto&a=s[i-1];const auto&b=s[i];
   if(T<=b.vt){out=a.value+(b.value-a.value)*std::clamp((T-a.vt)/std::max(.25,b.vt-a.vt),0.0,1.0);return true;}}
  out=s.back().value;return true;
 }
 // The delayed path is linear between commits, so its velocity changes in a
 // step every logic period whenever the player steers; each step jolted every
 // near roadside object by 1-4 px on that tick (owner session 23:35: |d2| p99
 // 7.2 units). Averaging the path over one period (a quadratic B-spline)
 // turns each step into a ramp of the same length: velocity is continuous.
 // It costs half a period of extra delay on the lateral only, never moves
 // beyond a confirmed value (the caller still clamps to the latest commit).
 bool smooth(unsigned tick,double delay,double width,double&out)const{
  double sum=0,value=0;
  for(int k=0;k<7;k++){if(!eval(tick,delay+width*k/6.0,value))return false;sum+=value;}
  out=sum/7;return true;
 }
}lateral_clock;
// Host-only telemetry beside the frame recorder (same switch: SMGP_RECORD or
// <project>/.local/record.on). One CSV line per tick of play: the cartridge's own
// committed values next to what the presentation shows. Read-only.
FILE*telemetry_file(){
 static FILE*file=nullptr;static int state=0;
 if(state)return file;
 state=-1;
 try{
  std::filesystem::path root;
  char*base=SDL_GetBasePath();std::filesystem::path dir=base?base:".";if(base)SDL_free(base);
  for(int up=0;up<8&&!dir.empty();up++){
   if(std::filesystem::exists(dir/".tools"/"ffmpeg"/"ffmpeg.exe")){root=dir;break;}
   dir=dir.parent_path();
  }
  if(root.empty())return nullptr;
  if(!getenv("SMGP_RECORD")&&!std::filesystem::exists(root/".local"/"record.on"))return nullptr;
  std::filesystem::create_directories(root/".local"/"recordings");
  char stamp[64];time_t now=time(nullptr);strftime(stamp,sizeof stamp,"%Y%m%d-%H%M%S",localtime(&now));
  auto path=root/".local"/"recordings"/(std::string("telemetry-")+(game_index?"smgp2-":"smgp1-")+stamp+".csv");
  file=fopen(path.string().c_str(),"w");
  if(file){state=1;fprintf(file,"tick,native_lateral,presented_lateral,native_phase,presented_phase,speed,steer,gear,interrupted\n");}
 }catch(...){file=nullptr;}
 return file;
}
double panorama_difference(double a,double b){
 // Native 16.16 accumulators wrap after 65536 pixels, not after one 1024px
 // map. Keep the fractional carry across the signed 32-bit boundary.
 double d=a-b;while(d>32768)d-=65536;while(d<-32768)d+=65536;return d;
}
double native_panorama(){
 // World 678E adds into 9222 and copies its high word to 9220 at 6792.
 // GP II EC5E adds into FC62 and copies its high word to FC66 at EC62.
 // Reading only the high-word copies lost the native fractional movement.
 return int32_t(rl(game_index?0xfc62:0x9222))/65536.0;
}
double present_panorama(){
 return render_clock.valid?render_clock.panorama:panorama_scroll;
}
// World queues a roadside impact before consuming its steering impulse at
// 7BE2..7C42. Do not extrapolate through that impulse or a native standstill:
// the previous moving sample is no longer a valid forecast. Read-only; the
// confirmed camera position still advances whenever the cartridge advances it.
// GP II: an impact flag with the car still moving does not stop the camera.
// Native deceleration is already in the observed phase; freezing the clock
// here stalled every roadside prop for 2-3 ticks, then snapped it forward.
bool camera_interrupted(){return game_index?!rw(0xfc2a):(!rw(0x9108)||rw(0xfc52)||rw(0xfc5c)||rw(0xfcbe));}
double present_phase(){
 if(render_clock.valid)return render_clock.phase;
 if(!observed_valid)return camera_phase;
 return observed_phase+((rw(profile().pause)||camera_interrupted())?0:observed_velocity*std::min(3u,tick-observed_stamp));
}
unsigned romw(unsigned a){return a+1<rom_limit()?(g_rom[a]<<8)|g_rom[a+1]:0;}
unsigned roml(unsigned a){return a+3<rom_limit()?(romw(a)<<16)|romw(a+2):0;}
double native_camera_phase(){
 double lap=rl(profile().player+26)/65536.0;
 return lap+phase_difference(rl(profile().player+30)/65536.0,lap);
}
unsigned tunnel_phase(){return rw(game_index?0xfc84:0x9238);}
unsigned last_tunnel_tick=0;
std::vector<unsigned> exit_backdrop_actors;
std::array<bool,224> exit_lintel_sky{};
// Owner 0.21af: the GP II exit portal drawn by the host (delayed clock, other
// size) did not match the cartridge's black frame around the opening: ragged
// steps and ground strips from the arch until the exit. Inside the tunnel and
// for 60 ticks after it, the cartridge's own exit sprites are kept.
bool gp2_native_exit(){
 static const bool host=[]{const char*o=getenv("SMGP_GP2_EXIT_HOST");return o&&o[0]=='1';}();
 // Same defect and same cure in the World (green ground blocks beside the
 // opening, sweep w-af4 steps 386-400): both cartridges keep their exit frame.
 if(host)return false;
 unsigned ph=tunnel_phase();return ph>=2||(ph==0&&last_tunnel_tick&&tick-last_tunnel_tick<=60);
}
bool structure_actor(unsigned a){
 // Reserved native left/right structure slots, separate from roadside props.
 // Eight 9-pose ROM families: starting arch, arches and tunnel portals.
 // The rear arch component is 6AC/10920, one table before the front one.
 unsigned first=game_index?0xdbd0:0xafc0,base=game_index?0x6ac:0x10920;
 if(a!=first&&a!=first+profile().stride)return false;
 unsigned family=rl(a+8),widths=rl(a+48);
 return rl(a)&&rl(a+4)&&family>=base&&family<=base+7*36&&(family-base)%36==0&&widths>=0x200&&widths+320<rom_limit();
}
// The tunnel light fittings deliberately use a zero-width RAM table, not a
// parent pointer. Qualify their routine AND family instead of accepting every
// transient object in the same pool as roadside scenery.
// GP II tunnel ceiling lamps (routine 151B4, family 814, lateral 0). They hang
// from the roof; the prop projection anchors objects to the road, so they stay
// on the cartridge's own sprites, which place them correctly.
// Owner 0.21ad: the native lamps jump between discrete poses and were drawn
// over the HUD. They are now host actors with continuous zoom hung from the
// roof (gp2_lamp_height above the road), like the World fittings.
// SMGP_GP2_LIGHTS_NATIVE=1 restores the cartridge sprites (diagnosis).
bool gp2_lamp(unsigned a){return game_index&&rl(a)==0x151b4;}
bool gp2_ceiling_light(unsigned a){static const bool native=[]{const char*o=getenv("SMGP_GP2_LIGHTS_NATIVE");return o&&o[0]=='1';}();return native&&gp2_lamp(a);}
double gp2_lamp_height(){static const double h=[]{const char*o=getenv("SMGP_GP2_LAMP_H");return o?atof(o):158.0;}();return h;}
bool tunnel_fitting(unsigned a){return !game_index&&rl(a)==0xa9b0&&rl(a+8)==0x108d8&&rl(a+48)==0xff5980;}
std::array<std::array<bool,2>,224> facade_edge{};
uint32_t plane_b(const GVDP*v,int x,int y);
// GP II Monaco entrance facade: the cartridge's flat light grey B material.
bool gp2_facade(uint32_t pixel){return (pixel&0xffffffu)==0xcecece;}
// A B raster row that is wall-black across the whole native width.
bool gp2_black_row(const GVDP*v,int line,int hs,int native_width){
 for(int k=0;k<=8;k++)if(plane_b(v,k*(native_width-1)/8-hs,line+v->vsram[1])!=0xff000000u)return false;
 return true;
}
// Owner 0.21af (video 001331-04, entrance): the facade's black base/shadow at
// the native edge was repeated across the whole 16:9 margin as long black bars
// that switched on and off with the edge. Outside the native columns the wall
// continues: a black edge pixel on a row that is NOT black across the whole
// native width takes the nearest facade grey above it on the same edge.
uint32_t gp2_entrance_margin(const GVDP*v,int line,int side,int native_width,uint32_t edge){
 if(edge!=0xff000000u)return edge;
 unsigned hb=(v->reg[13]&63)<<10;
 if(gp2_black_row(v,line,(int16_t)word(v,hb+line*4+2),native_width))return edge;
 for(int y=line-1;y>=65&&y>=line-48;y--){
  uint32_t p=plane_b(v,(side?native_width-1:0)-(int16_t)word(v,hb+y*4+2),y+v->vsram[1]);
  if(gp2_facade(p))return p;
  if(p!=0xff000000u)break;
 }
 return edge;
}
// GP II exit (phase 3): does the authored opening already reach this native
// edge on this raster line? True when the edge column of the B raster is open
// (not wall) from this line up to a panorama row without crossing wall, i.e.
// the hole itself touches the edge. Inside the tunnel only the road at the
// bottom reaches the edges, and it never connects to a panorama row.
bool gp2_exit_open(const GVDP*v,int line,int side,int native_width){
 unsigned hb=(v->reg[13]&63)<<10;
 for(int y=line;y>=65;y--){
  int hs=(int16_t)word(v,hb+y*4+2),sy=(y+v->vsram[1])&511,open=0;
  // Four edge columns: a single dark texel of the road edge or a tile seam
  // in column 0 must not read as wall.
  for(int k=0;k<4;k++)open+=plane_b(v,(side?native_width-1-k:k)-hs,y+v->vsram[1])!=0xff000000u;
  if(open<2)return false;
  if(sy>=40&&sy<112)return true;
 }
 return false;
}
double ray(const double*values,int count,double q){
 if(q<0)return values[0];
 if(q>=count-1){int span=std::min(8,count-1);return values[count-1]+(q-count+1)*(values[count-1]-values[count-1-span])/span;}
 int i=std::min((int)q,count-2);return values[i]+(values[i+1]-values[i])*(q-i);
}
// Re-evaluate the cartridge's read-only course geometry at the common camera
// phase. 6C06..6CD6 / F0B6..F192 quantize longitudinal input to four units,
// then clip distant curvature to a VDP storage sentinel. Differencing that
// sentinel as screen motion produced tens-of-pixels sideways jumps. The
// host retains the original map/templates/weights, continuous phase and
// full precision; the guest arrays, CPU and SAT remain untouched.
bool course_curve(double phase,std::array<double,96>&result,unsigned alternate_map=0){
 unsigned map=game_index?0x36c0:0x5b01,table=game_index?0x2222a:0x7053c;
 unsigned weights=game_index?0xf748:0x6ebe,runs=game_index?0xf798:0x6f0e;
 if(alternate_map)map=alternate_map;
 int index=int(std::floor(phase/4)),last=-1,source=0,origin=0,value=0;uint32_t slope=0;
 std::array<double,40> points;points.fill(std::numeric_limits<double>::quiet_NaN());
 result.fill(std::numeric_limits<double>::quiet_NaN());
 for(int n=0;n<40;n++){
  if(index<0||map+unsigned(index)>=65536)return false;
  int code=g_ram[map+index++];
  if(code>=128){if(game_index)return false;index=1;code=g_ram[map];if(code>=128)return false;}
  if(code!=last){last=code;int kind=code&63;if(kind)source=int(roml(table+kind*4));origin=int16_t(slope);if(!(slope&0x80000000u))origin=int16_t(-origin);}
  if(!code)value=int16_t(value+origin);
  else{
   if(source<0x200||unsigned(source+2)>=rom_limit())return false;
   int sample=romw(source);source+=2;if(sample>=32768)break;
   if(code&64){slope=0x80000000u|uint16_t(sample+origin);value=int16_t(value+int16_t(slope));}
   else{slope=uint16_t(sample-origin);value=int16_t(value-int16_t(slope));}
  }
  double projected=double(int16_t(value))*romw(weights+(39-n)*2)/65536.0;
  // The native multiply stops this ray chain beyond +/-256; it is an
  // invisible bend, not a wrapped signed coordinate to draw on the other side.
  if(std::abs(projected)>=256)break;
  points[n]=projected;
 }
 int cursor=96;double current=0;
 for(int n=0;n<40;n++){
  if(!std::isfinite(points[n]))break;
  int count=int(romw(runs+n*2));if(count<0||count>96||count>cursor)return false;
  if(!count)continue;
  if(count==1){current=points[n];result[--cursor]=current;}
  else{double step=(points[n]-current)/count;for(int j=0;j<count;j++){result[--cursor]=current;current+=step;}current=points[n];}
 }
 return cursor<96;
}
double monotone(double a,double b,double c,double d,double t){
 if(!std::isfinite(a))a=b;if(!std::isfinite(d))d=c;
 double delta=c-b,prev=b-a,next=d-c;
 double m0=prev*delta>0?2*prev*delta/(prev+delta):0,m1=delta*next>0?2*delta*next/(delta+next):0;
 return (2*t*t*t-3*t*t+1)*b+(t*t*t-2*t*t+t)*m0+(-2*t*t*t+3*t*t)*c+(t*t*t-t*t)*m1;
}
void prepare_centers(){
 precise_center=precise_ground=false;object_center_ready=false;
 if(!smooth_zoom)return;
 double phase=present_phase(),base=std::floor(phase/4)*4,t=(phase-base)/4;
 std::array<double,96>a{},b{},before{},after{};before.fill(NAN);after.fill(NAN);
 if(!course_curve(base,a)||!course_curve(base+4,b))return;
 course_curve(base-4,before);course_curve(base+8,after);
 double lateral=render_clock.valid?render_clock.lateral:observed_lateral;
 for(int q=0;q<120;q++){
  presentation_ray_valid[q]=q>=96||(std::isfinite(a[q])&&std::isfinite(b[q]));
  double curve=q>=96?0:(presentation_ray_valid[q]?monotone(before[q],a[q],b[q],after[q],t):256);
  presentation_center[q]=128+lateral*(q+6)/100+curve;
 }
 precise_center=true;
 // Scenery is placed from a spatially smoothed copy of this table. The cartridge
 // quantises curvature to 4-unit course blocks, so far rows hold a sawtooth
 // (a few px up/down every ~3 rows). A prop crossing those rows moves with the
 // sawtooth, which reads as zigzag when a line of objects approaches in a bend.
 // The road itself keeps the native table; only object centres are filtered,
 // with a triangular kernel one block period wide (none for the near rows).
 // Rows the cartridge marks invalid (bend beyond +/-256) carry a sentinel, and
 // the valid frontier moves one row at a time as the course advances. Keep the
 // sentinel out of every object lookup: extrapolate those rows from the first
 // valid ones and never let the filter reach across the frontier.
 std::array<double,120> bend{};std::array<bool,120> known{};
 int frontier=0;
 for(int q=0;q<120;q++){known[q]=presentation_ray_valid[q];bend[q]=presentation_center[q]-128-lateral*(q+6)/100;}
 while(frontier<119&&!known[frontier])frontier++;
 for(int q=frontier-1;q>=0;q--){double slope=frontier+1<120?bend[frontier+1]-bend[frontier]:0;bend[q]=bend[q+1]-slope;}
 for(int q=frontier+1;q<120;q++)if(!known[q])bend[q]=bend[q-1];
 for(int q=0;q<120;q++){
  double period=std::clamp(8.0*(q+6)*(q+6)/1010.0,1.5,12.0),fade=std::clamp((64.0-q)/24.0,0.0,1.0);
  double half=std::min(period,std::max(0.0,double(q-frontier)));
  fade*=std::clamp((q-frontier-1.0)/period,0.0,1.0);
  if(fade<=0||half<.5){object_center[q]=128+lateral*(q+6)/100+bend[q];continue;}
  double sum=0,weight=0;
  for(int k=-7;k<=7;k++){
   double w=8-std::abs(k),at=std::clamp(q+k*half/8.0,double(frontier),119.0);
   int i0=std::min(int(at),118);double v=bend[i0]+(bend[i0+1]-bend[i0])*(at-i0);
   sum+=w*v;weight+=w;
  }
  double smooth=bend[q]+(sum/weight-bend[q])*fade;
  object_center[q]=128+lateral*(q+6)/100+smooth;
 }
 if(getenv("SMGP_PROJECTION_TRACE")){fprintf(stderr,"[SMGP FAR] frame=%u t=%.2f base=%.0f B:",tick,t,base);for(int q=0;q<=14;q++)fprintf(stderr," %s%.0f",known[q]?"":"!",bend[q]);fprintf(stderr," | O:");for(int q=0;q<=14;q++)fprintf(stderr," %.0f",object_center[q]-128-lateral*(q+6)/100);fprintf(stderr,"\n");}
 object_center_ready=true;
 std::array<double,96>ha{},hb{},hp{},hn{};hp.fill(NAN);hn.fill(NAN);unsigned height_map=game_index?0x5240:0x7301;
 if(course_curve(base,ha,height_map)&&course_curve(base+4,hb,height_map)){
  course_curve(base-4,hp,height_map);course_curve(base+8,hn,height_map);
  for(int q=0;q<96;q++)presentation_ground[q]=std::isfinite(ha[q])&&std::isfinite(hb[q])?223-(95-q)*.5-monotone(hp[q],ha[q],hb[q],hn[q],t):world_ground[q];
  precise_ground=true;
 }
 
 if(getenv("SMGP_PROJECTION_TRACE"))fprintf(stderr,"[SMGP RAYS] frame=%u phase=%.6f lateral=%.6f curve0=%.6f curve10=%.6f precise=1 t=%.4f c30=%.4f c45=%.4f c55=%.4f c62=%.4f c75=%.4f c90=%.4f v30=%d v45=%d v55=%d v62=%d\n",tick,phase,lateral,presentation_center[0],presentation_center[10],t,presentation_center[30]-128-lateral*.36,presentation_center[45]-128-lateral*.51,presentation_center[55]-128-lateral*.61,presentation_center[62]-128-lateral*.68,presentation_center[75]-128-lateral*.81,presentation_center[90]-128-lateral*.96,(int)presentation_ray_valid[30],(int)presentation_ray_valid[45],(int)presentation_ray_valid[55],(int)presentation_ray_valid[62]);
}
double present_ground(double q){return precise_ground?ray(presentation_ground.data(),96,q):ray(world_ground.data(),96,q);}
double present_center(double q){return precise_center?ray(presentation_center.data(),120,q):ray(world_center.data(),120,q)+ray(center_velocity.data(),120,q)*camera_age();}
double present_object_center(double q){
 if(!(precise_center&&object_center_ready))return present_center(q);
 if(q<=0)return object_center[0];
 if(q>=119)return ray(object_center.data(),120,q);
 // Monotone cubic in q: a piecewise-linear table changes slope at every row, and
 // a prop crossing a row speeds up or slows down there.
 int i=int(q);const double nan=std::numeric_limits<double>::quiet_NaN();
 return monotone(i>0?object_center[i-1]:nan,object_center[i],object_center[i+1],i+2<120?object_center[i+2]:nan,q-i);
}
void mark_crisp_interior(const std::vector<uint32_t>&pixels,int w,int h,const SDL_FRect&dst,int width,int height,bool masked,bool exact,double ground_clip=224){
 // A single 3x footprint moves scenery texels fractionally without source
 // mip/bilinear blur. Vehicles and lenses retain their exact centre samples.
 // Original sprite texels use point sampling at the logical pixel centre.
 // Average coverage only at the silhouette, keeping fractional movement but
 // preserving authored tyre bands/letters inside an opaque sprite. Averaging
 // every interior subpixel made moving tread look like a static grey smear.
 int x0=std::max(0,int(std::floor(dst.x))),x1=std::min(width,int(std::ceil(dst.x+dst.w)));
 int y0=std::max(65,int(std::floor(dst.y))),y1=std::min(height,int(std::ceil(dst.y+dst.h)));
 for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){
  if(y+1>ground_clip)continue;
  if(masked&&tunnel_occlusion[y*width+x])continue;
  bool any=false,opaque=true;
  for(int yy=0;yy<coverage_scale;yy++)for(int xx=0;xx<coverage_scale;xx++){
   double u=(x+(xx+.5)/coverage_scale-dst.x)*w/dst.w,v=(y+(yy+.5)/coverage_scale-dst.y)*h/dst.h;
   unsigned alpha=(u>=0&&v>=0&&u<w&&v<h)?pixels[int(v)*w+int(u)]>>24:0;
   any|=alpha!=0;opaque&=alpha==255;
  }
  if(any)crisp_interior[y*width+x]=opaque&&exact;
 }
}
void mark_original_glass(const PaintProbe&glass,const SDL_FRect&dst,int width,int height){
 if(glass.roles.empty())return;
 int x0=std::max(0,int(std::floor(dst.x))),x1=std::min(width,int(std::ceil(dst.x+dst.w)));
 int y0=std::max(0,int(std::floor(dst.y))),y1=std::min(height,int(std::ceil(dst.y+dst.h)));
 for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){
  int u=int((x+.5-dst.x)*glass.width/dst.w),v=int((y+.5-dst.y)*glass.height/dst.h);
  if(u>=0&&v>=0&&u<glass.width&&v<glass.height&&glass.roles[v*glass.width+u])crisp_interior[y*width+x]=1;
 }
}
void snapshot_world(){
 const auto&pr=profile();unsigned player=pr.player;
 pending_stamp=building_stamp;
 pending_measured_at=tick;
 pending_phase=native_camera_phase();
 pending_panorama=native_panorama();
 for(int q=0;q<120;q++)pending_center[q]=256+(int16_t)rw(pr.center+q*2);
 for(int q=0;q<96;q++)pending_ground[q]=223-(int16_t)rw(pr.ground+q*2);
 pending_objects.clear();
 for(unsigned a=game_index?0xdbd0:0xafc0;a<pr.props_end;a+=pr.stride){
  bool car=a>=pr.cars&&a<pr.cars_end,prop=a>=pr.props,structure=smooth_zoom&&structure_actor(a);if((!car&&!prop&&!structure)||!rl(a))continue;
  if(prop&&!tunnel_fitting(a)&&((!game_index&&rl(a)<0xa174)||(game_index&&rl(a)<0x14954)))continue;
  if(gp2_ceiling_light(a))continue; // ceiling fixture: ground-anchored scaling put it on the road
  // Both relative Z and the camera reference come from the same absolute
  // position commit. GP II can commit +30 one VBlank before its +26 copy;
  // mixing the two made fixed scenery jump in scale at high speed.
  double gap=car?((double)rl(a+30)-rl(player+30))/65536.0:(int16_t)(rw(a+30)-rw(player+30))-(rl(player+30)&65535)/65536.0;
  if(car){double length=rw(pr.length);if(length<100)continue;while(gap>length*.5)gap-=length;while(gap<-length*.5)gap+=length;}
  // Once an actor's world origin is behind the player it belongs to the
  // mirror. A forward billboard must not grow through the camera afterwards.
  if(gap>170||gap<(smooth_zoom&&prop?-32:(smooth_zoom&&(!game_index||structure)?-8:0)))continue;
  unsigned family=rl(a+8);double lateral=(int16_t)rw(a+18);
  if(car&&gap<0&&std::abs(lateral-(int16_t)rw(player+18))<76)continue;
  if(car){
   double scale=10.0/std::max(.8,gap+4),q=std::clamp(scale*101-6.0,0.0,119.0);
   int orient=8;
   if(q>=3){int d=(int16_t)rw(pr.curve+2*std::max(0,(int)q-3))-(int16_t)rw(pr.curve+2*(int)q);orient+=d<0?-((-d)&~3):(d&~3);
    int lane=(int)lateral-(int16_t)rw(player+18),side=lane<0?4:-4;
    if(std::abs(lane)>=76)orient+=side;if(std::abs(lane)>=220)orient+=side;
   }
   if(g_ram[a+16])orient=(int8_t)g_ram[a+17];
   family=roml(pr.orient+(std::clamp(orient,0,16)&~3)+(game_index&&rw(a+12)?20:0));
  }else{
   unsigned widths=rl(a+48);if(tunnel_fitting(a)||gp2_lamp(a))lateral=0;
   else {if(widths<0x200||widths+320>=rom_limit())continue;
    lateral=(int16_t)romw(widths+318)*(lateral<0?-1:1);}
  }
  if(family<0x200||family+36>=rom_limit())continue;
  unsigned mapping=roml(family+32);if(mapping<0x200||mapping+2>=rom_limit()||romw(mapping)>63)continue;
  ObjectPose p{a,rl(a),mapping,0,0,game_index?0:(int16_t)rw(a+12),0};p.family=family;p.stamp=building_stamp;p.gap=gap;p.lateral=lateral;p.phase=pending_phase;p.distance=rl(a+26)/65536.0;p.architecture=structure;
  pending_objects.push_back(p);
 }
 // Complete, observed motion samples arrive independently of the native SAT
 // transfer. Keep camera/road/actors together. These samples never authorize
 // removal of native sprites; only an exact SAT match does that below.
 UploadedScene motion;motion.serial=build_serial;motion.stamp=pending_stamp;motion.measured_at=pending_measured_at;
 motion.objects=pending_objects;motion.center=pending_center;motion.ground=pending_ground;
 motion.phase=pending_phase;motion.panorama=pending_panorama;motion_queue.push_back(std::move(motion));
 while(motion_queue.size()>6)motion_queue.pop_front();
}
bool world_object(unsigned a){
 const auto&p=profile();
 if(structure_actor(a))return true;
 if(a>=p.cars&&a<p.cars_end)return true;
 if(a<p.props||a>=p.props_end)return false;
 // This pool also holds transient car effects with a RAM parent pointer at
 // +30. They are not roadside actors; their builder does not use prop Z.
 unsigned routine=rl(a);if((!game_index&&routine<0xa174)||(game_index&&routine<0x14954))return false;
 if(gp2_ceiling_light(a))return false;
 unsigned widths=rl(a+48);return tunnel_fitting(a)||gp2_lamp(a)||(widths>=0x200&&widths+320<rom_limit());
}
void accepted_piece(unsigned object,unsigned address){
 unsigned base=game_index?0xf600:0x9ac0;object&=65535;address&=65535;
 if(address<base||address>=base+640||(address-base)%8)return;
 formation_building[(address-base)/8]=formation_object_team(object);
 if(getenv("SMGP_BANNER_TRACE")&&(game_index?rw(0xfe1c):rw(0xfc80)))fprintf(stderr,"[SMGP UI OWNER] frame=%u object=%04X routine=%X map=%X slot=%u\n",tick,object,rl(object),rl(object+4),(address-base)/8);
 if(world_object(object)||flagman_actor(object)){
  if(rw(object+14)&0x8000)building_mirror_owner[(address-base)/8]=object;
  else building_owner[(address-base)/8]=object;
 }else if(player_actor(object)&&player_arcade())building_owner[(address-base)/8]=object;
 if(getenv("SMGP_OWNER_TRACE")&&player_actor(object))fprintf(stderr,"[SMGP PLAYER OWNER] frame=%u object=%04X slot=%u map=%X\n",tick,object,(address-base)/8,rl(object+4));
}
void seal_scene(){
 if(game_index&&formation_page()){
  FormationUpload f;f.serial=formation_serial;f.teams=formation_building;std::copy_n(g_ram+0xf600,640,f.sat.begin());
  if(!formation_queue.empty()&&formation_queue.back().serial==f.serial)formation_queue.back()=f;
  else formation_queue.push_back(f);
  while(formation_queue.size()>12)formation_queue.pop_front();
 }
 if(!objects_ready)return;
 UploadedScene s;s.serial=build_serial;s.owner=building_owner;s.mirror_owner=building_mirror_owner;s.objects=pending_objects;
 s.center=pending_center;s.ground=pending_ground;s.phase=pending_phase;s.panorama=pending_panorama;s.stamp=pending_stamp;s.measured_at=pending_measured_at;
 auto*v=&g_machine.vdp;s.sat_base=(v->reg[5]&((v->reg[12]&1)?0x7e:0x7f))<<9;
 unsigned base=game_index?0xf600:0x9ac0;
 std::copy_n(g_ram+base,640,s.sat.begin());
 if(!scene_queue.empty()&&scene_queue.back().serial==s.serial)scene_queue.back()=std::move(s);
 else scene_queue.push_back(std::move(s));
 while(scene_queue.size()>12)scene_queue.pop_front();
}
bool matches_upload(const UploadedScene&s,const GVDP*v){
 unsigned base=(v->reg[5]&((v->reg[12]&1)?0x7e:0x7f))<<9;if(base!=s.sat_base)return false;
 std::array<bool,80> visited{};unsigned slot=0;
 for(unsigned n=0;n<80;n++){
  if(slot>=80||visited[slot])return false;visited[slot]=true;
  for(unsigned b=0;b<8;b++)if(v->vram[(base+slot*8+b)&65535]!=s.sat[slot*8+b])return false;
  unsigned next=s.sat[slot*8+3]&127;if(!next)return true;slot=next;
 }
 return false;
}
bool acquire_uploaded_scene(const GVDP*v){
 for(auto it=scene_queue.rbegin();it!=scene_queue.rend();++it)if(matches_upload(*it,v)){
  removal_objects=it->objects;removal_phase=it->phase;
  const auto&motion=motion_queue.empty()?*it:motion_queue.back();
  if(committed_serial!=motion.serial)publish_scene(motion);
  displayed_owner=it->owner;displayed_mirror_owner=it->mirror_owner;return true;
 }
 displayed_owner.fill(0);displayed_mirror_owner.fill(0);removal_objects.clear();return false;
}
void decode_pixels(Piece&p,const GVDP*v,unsigned mapping,unsigned piece,bool stable){
 uint64_t key=((uint64_t)game_index<<47)|((uint64_t)mapping<<25)|((uint64_t)piece<<19)|((uint64_t)p.tile<<8)|(p.pal<<6)|(p.flip<<4)|((p.w/8-1)<<2)|(p.h/8-1);
 auto cached=prop_source_tiles.find(key);
 if(stable&&cached!=prop_source_tiles.end()){
  for(int i=0;i<p.w*p.h;i++){unsigned index=cached->second[i];p.pixels[i]=index==255?0:genesis_dac_cram_to_argb(v->cram[index],GENESIS_DAC_NORMAL);}return;
 }
 std::array<uint8_t,1024> indexed{};indexed.fill(255);
 for(int y=0;y<p.h;y++)for(int x=0;x<p.w;x++){
  int sx=(p.flip&1)?p.w-1-x:x,sy=(p.flip&2)?p.h-1-y:y;
  unsigned tile=(p.tile+(sx/8)*(p.h/8)+sy/8)&2047;
  unsigned byte=v->vram[(tile*32+(sy&7)*4+(sx&7)/2)&65535];
  unsigned nib=(sx&1)?byte&15:byte>>4;
  indexed[y*p.w+x]=nib?(p.pal*16+nib)&63:255;
  p.pixels[y*p.w+x]=nib?genesis_dac_cram_to_argb(v->cram[(p.pal*16+nib)&63],GENESIS_DAC_NORMAL):0;
 }
 if(stable)prop_source_tiles[key]=indexed;
}
bool mapping_piece(Piece&p,const ObjectPose&o,unsigned i){
 unsigned a=o.mapping+2+i*6;if(a+6>=rom_limit())return false;
 unsigned sz=g_rom[a+(game_index?3:1)],attr=(romw(a+(game_index?4:2))+o.tile)&65535;
 p.x=(int16_t)romw(a+(game_index?0:4));p.y=(int)g_rom[a+(game_index?2:0)]-256;
 p.w=(((sz>>2)&3)+1)*8;p.h=((sz&3)+1)*8;p.tile=attr&2047;p.pal=(attr>>13)&3;p.flip=(attr>>11)&3;return true;
}
// Owner 0.21ac: a rival whose foot projects above the topmost NATIVE road row
// stands on no drawn road (past the road end/horizon) and looked like it was
// floating. Hide it until its foot reaches the drawn road. The top row is the
// previous frame's (road_rows are composed after the groups). Hysteresis of
// one row so a car hovering on the edge does not blink. SMGP_FAR_CARS=0 disables.
std::map<unsigned,bool> far_car_state;
double course_grass_src();
bool far_car_hidden(unsigned object,double foot,double x,double scale){
 static const bool off=[]{const char*o=getenv("SMGP_FAR_CARS");return o&&o[0]=='0';}();
 if(off||tunnel_phase()!=0)return far_car_state[object]=false;
 int top=-1;for(int y=65;y<221;y++)if(road_rows[y].valid&&road_rows[y].road){top=y;break;}
 if(top<0)return far_car_state[object]=false;
 bool&hidden=far_car_state[object];
 double margin=hidden?.5:-.5;
 bool beyond=foot<top+margin;
 // Also beyond the road when, on the drawn row under its foot, the car's body
 // lies completely outside the native road surface (far end of a bend).
 double off_road=0;
 if(!beyond&&foot>=0&&foot<std::min(top+40,220)){
  const RoadRow&row=road_rows[int(foot)];
  if(row.valid&&row.road&&row.road_q>=0){
   double half=course_grass_src()*(row.road_q+6)/38;
   off_road=std::abs(x-row.road_center)-32*scale-half;
   beyond=off_road>(hidden?-1.0:1.0);
  }
 }
 hidden=beyond;
 if(getenv("SMGP_FARCAR_TRACE"))fprintf(stderr,"[SMGP FARCAR] frame=%u object=%04X foot=%.3f top=%d off=%.2f hidden=%d\n",tick,object,foot,top,off_road,hidden?1:0);
 return hidden;
}
void source_groups(const GVDP*v){
 if(!rw(profile().pause)){
  bool still=camera_velocity<.02;
  if(still&&!spark_still){spark_still=true;spark_still_since=tick;}
  if(!still&&spark_still){spark_still=false;if(tick-spark_still_since>=120)spark_start_tick=tick;}
 }
 for(const auto&o:world_objects){
  // A fresher measured pose may move an already proven actor, but cannot
  // introduce a new actor over an unrecognized native sprite or inherit a
  // recycled prop slot's ownership. The SAT mask retains exact provenance.
  auto proven=std::find_if(removal_objects.begin(),removal_objects.end(),[&](const ObjectPose&old){return old.object==o.object&&old.routine==o.routine&&((o.object<profile().props&&!o.architecture)||old.family==o.family);});
  if(proven==removal_objects.end())continue;
  Group g{};g.x=1e9;g.y=1e9;g.source=true;g.full=true;g.id=(int)o.object;g.signature=o.mapping;
  g.architecture=o.architecture||(!game_index&&(o.routine==0xa980||o.routine==0xa9b0));
  g.portal=o.architecture&&o.family>=(game_index?0x73cu:0x109b0u);
  if(g.portal&&getenv("SMGP_QA_INPUT_ONLY")&&getenv("SMGP_QA_HIDE_PORTALS"))g.culled=true;
  // 0.21ag: exit window. The host portal is not drawn; its native sprite is
  // removed from the front and its exact pixels go to the background instead
  // (see exit backdrop in the raster pass), so pillars, lamps and cars stay
  // in front of the cartridge's black frame (0.21af kept it as a sprite and
  // it covered them as black boxes).
  // Only the EXIT portal: in phase 2 the entrance jambs are still beside the
  // camera (small gap) and stay host actors.
  if(o.architecture&&gp2_native_exit()&&!(tunnel_phase()==2&&o.gap<40)){exit_backdrop_actors.push_back(o.object);g.culled=true;groups.push_back(g);continue;}
  if(o.object>=profile().cars&&o.object<profile().cars_end)for(int angle=0;angle<5;angle++)
   if(o.family==roml(profile().orient+angle*4)||(game_index&&o.family==roml(profile().orient+angle*4+20)))g.angle=angle;
  double age=camera_age();
  bool car=o.object>=profile().cars&&o.object<profile().cars_end;
  // Roadside props are stationary in world Z. Their distance must be measured
  // from the camera phase of THEIR OWN snapshot, along the smooth presentation
  // clock. Using the latest discrete camera sample (velocity 0 on some ticks)
  // froze the prop at one depth and then jumped it on the next upload.
  // Architecture and portals are stationary in world Z exactly like props: they
  // use the same smoothed Z, so their zoom no longer steps with snapshots.
  double gap=!car&&smooth_zoom&&o.phase!=0?phase_difference(o.zsmooth,present_phase()):o.gap+(camera_velocity+o.dgap)*age-phase_difference(present_phase(),camera_phase);
  // Arches and portals keep growing past the camera plane, exactly as props do,
  // until they leave the frame. Cutting them at gap 0 made them vanish in one tick.
  if(o.architecture&&gap+8<=.1){g.culled=true;groups.push_back(g);continue;}
  if(smooth_zoom&&!car){
   // A roadside billboard follows its ray through the near plane. Clamping
   // its depth/scale leaves a giant, frozen billboard beside the camera.
   if(gap+8<=.1){
    // The exact uploaded owner is still proven, but its presentation has
    // passed the near plane. Keep the removal record until that SAT epoch
    // retires; otherwise the old native light/pole flashes back into view.
    g.culled=true;groups.push_back(g);continue;
   }
   g.scale=10.0/(gap+8);
  }else{
   gap=std::max(smooth_zoom&&!game_index?-8.0:0.0,gap);
   g.scale=10.0/std::max(1.4,gap+(car?4:8));
  }
  double approach=std::clamp((38-std::max(0.0,gap))/38.0,0.0,1.0);
  bool shared_gp2=std::all_of(gp2_cars.begin(),gp2_cars.end(),[](const auto&v){return !v.empty();});
  g.zoom=car&&smooth_zoom&&!game_index&&!shared_gp2?1+.45*approach*approach*(3-2*approach):1;
  if(o.architecture){double near=std::clamp((80-gap)/80.0,0.0,1.0);g.zoom=1+.45*near*near*(3-2*near);}
  g.team=team_for_actor(o.object);g.wheel=(int)std::floor((o.distance+(camera_velocity+o.dgap)*age)*.16)&3;
  double distance=o.distance+(camera_velocity+o.dgap)*age;
  double speed=std::clamp(camera_velocity+o.dgap,0.0,3.0);
  if(car){
   // No invented roll/bounce on the original rear drawings. At large zoom
   // their subpixel oscillation became visible as a periodic tyre tremor.
   int qi=std::clamp((int)(g.scale*101-6),3,119);
   g.steer=(int16_t)rw(profile().curve+2*(qi-3))-(int16_t)rw(profile().curve+2*qi);
   auto&e=effect_clocks[o.object];if(e.routine!=o.routine){unsigned last=e.last_start;e={};e.last_start=last;e.routine=o.routine;e.stamp=tick;e.curve=int(g.steer);e.kerb=std::abs(o.lateral)>240;}
   if(!rw(profile().pause)&&e.stamp!=tick){
    if(e.age>=0&&++e.age>=(int)spark_cycle.size())e.age=-1;
    bool kerb=std::abs(o.lateral)>240;
    bool fixture=getenv("SMGP_QA_INPUT_ONLY")&&getenv("SMGP_QA_SPARK_CYCLE")&&tick-e.last_start>60;
    // Owner 0.21ac: the plank touches the ground only where a real F1 car
    // bottoms out: (a) high speed on straights (downforce), (b) the first
    // laps after the start (full tank), (c) bumps/kerbs and fast bends (G
    // load). A deterministic draw every 30 ticks per car, weighted by those
    // situations, and at least 3 s (180 ticks, owner) between bursts of the same car
    // AND between any two bursts on screen.
    // Rival speed here (phase units/tick): median ~0.47, p90 ~0.98, top ~2.
    bool fast=speed>=.9,bend=std::abs(g.steer)>=4;
    bool start=spark_start_tick&&tick-spark_start_tick<1800;
    bool bump=(kerb&&!e.kerb)||(bend&&std::abs(e.curve)<4);
    int chance=0; // percent per 30-tick window
    if(fast&&!bend)chance=25;
    if(fast&&bend)chance=45;
    if(bump&&speed>.7)chance=std::max(chance,50);
    if(start&&speed>.6)chance+=25;
    bool window=bump||((tick+o.object)%30==0);
    if(window&&getenv("SMGP_SPARK_TRACE"))fprintf(stderr,"[SMGP SPARK WINDOW] frame=%u object=%04X speed=%.3f gap=%.1f steer=%d bump=%d start=%d chance=%d\n",tick,o.object,speed,gap,int(g.steer),bump?1:0,start?1:0,chance);
    unsigned draw=(o.object*2654435761u)^(tick/30*40503u);draw^=draw>>13;draw*=0x5bd1e995u;draw^=draw>>15;
    bool natural=window&&chance>0&&int(draw%100)<chance
     &&(!e.last_start||tick-e.last_start>=180)&&(!spark_global_tick||tick-spark_global_tick>=180);
    if(gap>0&&gap<140&&e.age<0&&((speed>.65&&natural)||(fixture&&(!e.last_start||tick-e.last_start>120)))){e.age=0;e.last_start=tick;spark_global_tick=tick;}
    e.curve=int(g.steer);e.kerb=kerb;e.stamp=tick;
   }
   if(e.age>=0)g.spark=e.age+1;
  }
  g.passing_side=o.lateral-(int16_t)rw(profile().player+18)<0?-1:1;
  g.side=o.lateral<0?-1:1;g.world=o.zsmooth!=0?o.zsmooth:o.distance;
  double q=std::max(0.0,g.scale*101-6.0);
  // A native curve-template terminator denotes geometry beyond the visible
  // bend, not an X coordinate of +256. Do not animate actors along that
  // storage sentinel or bring their small native pieces back over the road.
  if(precise_center&&!presentation_ray_valid[std::clamp(int(q),0,119)]){
   if(getenv("SMGP_PROJECTION_TRACE"))fprintf(stderr,"[SMGP CULL] frame=%u object=%04X gap=%.6f reason=curve_occlusion q=%.6f\n",tick,o.object,gap,q);
   g.culled=true;groups.push_back(g);continue;
  }
  // Beyond the 120-entry curve table, ray() extrapolates with the steep near
  // slope of the last samples. q reaches 380-560 just before a roadside object
  // passes the camera, so the road centre flew 400-1900 px per tick and the
  // object jittered sideways at high speed in curves. Read the curve at its
  // last valid entry only; the object's own scale keeps growing, so it moves
  // off the screen edge along the lateral line, as the arcade does.
  double qc=std::min(q,119.0);
  g.road_x=present_object_center(qc);
  g.from_x=g.road_x+(o.lateral+o.dlateral*age)*g.scale;
  // The ground row, unlike the curve, is linear in q near the camera (ray()
  // extrapolates the last rows). Holding it at q=119 froze the foot while the
  // scale kept growing: an overtaken car (and a passing prop) rose upward at
  // the end of its zoom instead of leaving through the bottom of the frame.
  // Architecture keeps the former anchor (tunnel/portal masks are tuned to it).
  g.from_foot=present_ground(o.architecture?qc:q);g.z=gap;
  if(game_index&&smooth_zoom&&car&&q<95){
   // The projection can fold vertically at a crest. A farther car keeps its
   // own world ray, but nearer terrain hides its lower pixels. Curve culling
   // alone cannot establish this depth ordering; a far car otherwise drives
   // visibly across the road belonging to the nearer intersection.
   double crest=224;
   for(int near_q=int(std::floor(q))+1;near_q<=95;near_q++)crest=std::min(crest,present_ground(near_q));
   if(crest<g.from_foot-.125)g.ground_clip=crest;
   if(getenv("SMGP_PROJECTION_TRACE"))fprintf(stderr,"[SMGP CREST20] frame=%u object=%04X q=%.6f foot=%.6f clip=%.6f gap=%.6f\n",tick,o.object,q,g.from_foot,g.ground_clip,gap);
  }
  if(car&&smooth_zoom&&far_car_hidden(o.object,g.from_foot,g.from_x,g.scale)){
   if(getenv("SMGP_PROJECTION_TRACE"))fprintf(stderr,"[SMGP CULL] frame=%u object=%04X gap=%.6f reason=beyond_road_end foot=%.6f\n",tick,o.object,gap,g.from_foot);
   g.culled=true;groups.push_back(g);continue;
  }
  if(getenv("SMGP_PROJECTION_TRACE"))fprintf(stderr,"[SMGP PROJECT] frame=%u object=%04X q=%.6f center=%.6f velocity=%.6f age=%.2f lateral=%.6f lane_velocity=%.6f family=%X sample=%u phase=%.6f present=%.6f precise=%d\n",tick,o.object,q,present_object_center(q),precise_center?0:ray(center_velocity.data(),120,q),camera_age(),o.lateral,o.dlateral,o.family,o.stamp,camera_phase,present_phase(),precise_center);
  if(!game_index&&o.routine==0xa9b0&&o.family==0x108d8)g.from_foot-=158*g.scale;
  if(game_index&&o.routine==0x151b4)g.from_foot-=gp2_lamp_height()*g.scale;
  ObjectPose art=o;
  if(!smooth_zoom){
   int d=std::clamp((o.object<profile().cars_end?165:161)-(int)gap,0,161);
   art.mapping=roml(o.family+g_rom[(game_index?0x23cda:0x9c28)+d]);g.scale=1;
  }
  double right=-1e9,bottom=-1e9;
  size_t first_piece=pieces.size();
  int gi=(int)groups.size();unsigned count=romw(art.mapping)+1;
  for(unsigned i=0;i<count;i++){
   Piece p{};p.sat=-1;p.group=gi;if(!mapping_piece(p,art,i))break;
   g.x=std::min(g.x,(double)p.x);g.y=std::min(g.y,(double)p.y);
   right=std::max(right,(double)p.x+p.w);bottom=std::max(bottom,(double)p.y+p.h);
   if(!i)g.palette=p.pal;
   decode_pixels(p,v,art.mapping,i,o.object>=profile().props);pieces.push_back(p);
  }
  g.w=right-g.x;g.h=bottom-g.y;
  if(g.w>0&&g.h>0&&g.w<=512&&g.h<=256)groups.push_back(g);else pieces.resize(first_piece);
 }
 // A newer motion snapshot can retire a prop before its older, exact native
 // SAT leaves VRAM. Its removal belongs to THAT SAT, not to the newest pool.
 // Retain a removal-only group through retirement even if snapshot_world no
 // longer includes the passed object. Never infer ownership from a new slot.
 for(const auto&o:removal_objects){
  if((o.object<profile().props&&!o.architecture)||std::any_of(groups.begin(),groups.end(),[&](const Group&g){return g.id==(int)o.object;}))continue;
  double gap=o.gap-phase_difference(present_phase(),removal_phase);
  if(gap+8<=.1){Group g{};g.id=int(o.object);g.culled=true;g.full=true;groups.push_back(g);}
 }
}
void export_gp2_source(int gi,const Group&g,int w,int h){
 const char*dir=getenv("SMGP_GP2_EXPORT_DIR");
 if(!dir||!getenv("SMGP_QA_INPUT_ONLY")||game_index!=1||g.id!=0xdd10||g.angle<0||scene_fade(&g_machine.vdp)!=255)return;
 std::vector<uint8_t> ink(size_t(w)*h,255);
 const auto*v=&g_machine.vdp;
 for(auto it=pieces.rbegin();it!=pieces.rend();++it){const auto&p=*it;if(p.group!=gi)continue;
  for(int y=0;y<p.h;y++)for(int x=0;x<p.w;x++){
   int sx=(p.flip&1)?p.w-1-x:x,sy=(p.flip&2)?p.h-1-y:y;
   unsigned tile=(p.tile+(sx/8)*(p.h/8)+sy/8)&2047;
   unsigned byte=v->vram[(tile*32+(sy&7)*4+(sx&7)/2)&65535];unsigned nib=(sx&1)?byte&15:byte>>4;
   if(nib)ink[(int(p.y-g.y)+y)*w+int(p.x-g.x)+x]=p.pal*16+nib;
  }
 }
 uint64_t hash=1469598103934665603ull;for(auto b:ink){hash^=b;hash*=1099511628211ull;}
 if(getenv("SMGP_QA_PALETTE_CLOCK")){
  std::ofstream clock(std::filesystem::path(dir)/"palette.tsv",std::ios::app);clock<<tick<<' '<<g.angle<<' '<<g.wheel<<' '<<std::hex<<hash;
  for(int i=0;i<64;i++)clock<<' '<<v->cram[i];clock<<std::dec<<'\n';
 }
 static std::array<std::set<uint64_t>,5>saved;if(!saved[g.angle].insert(hash).second)return;
 char name[96];snprintf(name,sizeof(name),"angle%d-%016llx.sgpi",g.angle,(unsigned long long)hash);
 std::ofstream f(std::filesystem::path(dir)/name,std::ios::binary);int x=int(g.x),y=int(g.y);
 f.write("SGPI",4);f.write((char*)&w,4);f.write((char*)&h,4);f.write((char*)&x,4);f.write((char*)&y,4);
 std::array<uint32_t,64>palette;for(int i=0;i<64;i++)palette[i]=genesis_dac_cram_to_argb(v->cram[i],GENESIS_DAC_NORMAL);
 f.write((char*)palette.data(),256);f.write((char*)ink.data(),ink.size());f.close();
 std::ofstream index(std::filesystem::path(dir)/"observations.tsv",std::ios::app);
 index<<g.angle<<' '<<name<<' '<<tick<<' '<<g.wheel<<' '<<std::hex<<g.signature<<std::dec<<' '<<w<<' '<<h<<' '<<x<<' '<<y<<'\n';
}
// GP II tyre-stack sequences (owner request 0.21aa): blue/white (cartridge),
// red/white or green/white (arcade). The cartridge lays stacks in continuous
// lines every 16 course units; each block of 4 stacks (64 units) on one side
// takes one colour from a fixed hash of (course length, block, side), so the
// mix is random along the course but identical on every lap. No guest state.
int stack_colour(double distance,int side){
 unsigned length=rw(profile().length),block=unsigned(std::floor((distance+8)/64));
 uint32_t h=block*2654435761u^(side<0?0x9e3779b9u:0x7f4a7c15u)^(length*40503u);h^=h>>15;h*=2246822519u;h^=h>>13;
 int colour=int(h%3);
 if(getenv("SMGP_MOTION_TRACE"))fprintf(stderr,"[SMGP STACK RUN] distance=%.2f side=%d block=%u colour=%d\n",distance,side,block,colour);
 return colour;
}
bool tree_architecture_clear(double distance,unsigned length){
 // Decode the complete native course schedule before any tree becomes visible.
 // Portals live in reserved slots outside the ordinary roadside actor pool.
 // A live-only check therefore learned about the tunnel too late.
 if(std::abs(phase_difference(distance,0))<220)return false; // start/finish gantry
 unsigned table=rl(game_index?0xfcb0:0x9240),stride=game_index?6:4;
 if(table<0x200||table+stride>=rom_limit())return false;
 double entrance=-1;
 for(unsigned record=0;record<512;record++){
  unsigned at=table+record*stride;if(at+stride>rom_limit())return false;
  unsigned course=romw(at);if(course&0x8000)break;
  unsigned repeats=game_index?romw(at+2):g_rom[at+2];
  unsigned pointer=game_index?0x2479c+romw(at+4):0x713ec+g_rom[at+3]*4;
  unsigned start=roml(pointer);if(repeats>128||start<0x200||start>=rom_limit())return false;
  for(unsigned repeat=0;repeat<repeats;repeat++){
   unsigned p=start;
   for(unsigned n=0;n<2048;n++){
    if(p+2>rom_limit())return false;
    unsigned code=game_index?romw(p):g_rom[p];p+=game_index?2:1;
    if(code&(game_index?0x8000:0x80))break;
    bool arch=game_index?(code==4):(code==21);
    bool enter=game_index?(code==8):(code==22),exit=game_index?(code==12):(code==23);
    double point=double(course)+20;
    if((arch||enter||exit)&&std::abs(phase_difference(distance,point))<220)return false;
    if(enter)entrance=point;
    if(exit&&entrance>=0){if(distance>=entrance-220&&distance<=point+220)return false;entrance=-1;}
    course+=game_index?8:16;
   }
  }
 }
 return entrance<0||distance<entrance-220; // malformed/unclosed tunnel: keep clear
}
// Tunnel spans {entrance,exit} decoded from the native course schedule (same
// records tree_architecture_clear walks; both cartridges). Cached per course.
const std::vector<std::pair<double,double>>& tunnel_spans(){
 static std::vector<std::pair<double,double>> spans;static unsigned key=~0u;
 unsigned table=rl(game_index?0xfcb0:0x9240),stride=game_index?6:4,length=rw(profile().length);
 unsigned now=table^(length<<20)^(unsigned(game_index)<<31);if(now==key)return spans;
 key=now;spans.clear();
 if(table<0x200||table+stride>=rom_limit())return spans;
 double entrance=-1;
 for(unsigned record=0;record<512;record++){
  unsigned at=table+record*stride;if(at+stride>rom_limit())break;
  unsigned course=romw(at);if(course&0x8000)break;
  unsigned repeats=game_index?romw(at+2):g_rom[at+2];
  unsigned pointer=game_index?0x2479c+romw(at+4):0x713ec+g_rom[at+3]*4;
  unsigned start=roml(pointer);if(repeats>128||start<0x200||start>=rom_limit())break;
  for(unsigned repeat=0;repeat<repeats;repeat++){
   unsigned p=start;
   for(unsigned n=0;n<2048;n++){
    if(p+2>rom_limit())break;
    unsigned code=game_index?romw(p):g_rom[p];p+=game_index?2:1;
    if(code&(game_index?0x8000:0x80))break;
    bool enter=game_index?(code==8):(code==22),exit=game_index?(code==12):(code==23);
    double point=double(course)+20;
    if(enter)entrance=point;
    if(exit&&entrance>=0){spans.push_back({entrance,point});entrance=-1;}
    course+=game_index?8:16;
   }
  }
 }
 return spans;
}
// 0 = open air, 1 = fully inside. Smooth over `reach` course units before an
// entrance and after an exit, from the player's continuous phase (no ticks).
double tunnel_veil(){
 const double reach=56;double phase=present_phase(),veil=0;
 auto ease=[](double t){t=std::clamp(t,0.0,1.0);return t*t*(3-2*t);};
 for(const auto&s:tunnel_spans()){
  double to_enter=phase_difference(s.first,phase),to_exit=phase_difference(s.second,phase);
  if(to_enter>0&&to_enter<reach)veil=std::max(veil,ease(1-to_enter/reach));
  else if(to_enter<=0&&to_exit>0)veil=1;
  else if(to_exit<=0&&-to_exit<reach)veil=std::max(veil,ease(1+to_exit/reach));
 }
 return veil;
}
// Owner 0.21ad (GP II tunnel approach): the cartridge paints the entrance
// facade (flat grey wall) with the B raster only from tunnel phase 1, where
// it appears at once 13 rows tall, then grows in whole-row steps. Measured in
// the original output (research_gp2_tunnel20 --original): wall height in rows
// follows perspective, rows = K/(to_enter + C). The host continues that same
// wall (a) BEFORE phase 1, fading in from far away, and (b) during phase 1
// where its continuous top is above the native stepped top. Read-only.
struct FacadeBand{bool on=false;double top=0,alpha=0;int bottom=0;};
constexpr double gp2_facade_offset=38.5; // native wall plane vs schedule entrance (fit, max err 0.8 row)
int facade_bottom_row=-1; // native facade base, measured from the B raster
FacadeBand gp2_facade_band(){
 FacadeBand b;
 if(!game_index||!smooth_zoom||tunnel_phase()>1)return b;
 const char*o=getenv("SMGP_FACADE_BAND");if(o&&o[0]=='0')return b;
 double phase=present_phase(),to_enter=1e9;
 for(const auto&s:tunnel_spans()){double d=phase_difference(s.first,phase);if(d>0&&d<to_enter)to_enter=d;}
 if(to_enter>600)return b;
 constexpr double K=2311,C=gp2_facade_offset;
 double rows=K/std::max(1.0,to_enter+C);
 b.bottom=facade_bottom_row>0?facade_bottom_row:175;
 b.top=b.bottom+1-rows;
 // Fade in over the far approach; opaque well before the native wall appears.
 double fade=std::clamp((460-(to_enter+C))/220.0,0.0,1.0);b.alpha=fade*fade*(3-2*fade);
 b.on=b.alpha>0;
 if(getenv("SMGP_FACADE_TRACE"))fprintf(stderr,"[SMGP FACADE] frame=%u phase=%u present=%.2f to_enter=%.2f rows=%.2f top=%.2f bottom=%d alpha=%.3f\n",tick,tunnel_phase(),phase,to_enter,rows,b.top,b.bottom,b.alpha);
 return b;
}
void plan_trees(){
 if(!smooth_zoom||!observed_valid)return;
 unsigned length=rw(profile().length);if(length<300||length>30000)return;
 if(planted_trees.empty()||tree_track_length!=length){
  tree_track_length=length;tree_seed=2166136261u^unsigned(game_index);unsigned base=game_index?0x36c0:0x5b01;
  for(unsigned i=0;i<(length+3)/4&&base+i<65536;i++){tree_seed^=g_ram[base+i];tree_seed*=16777619u;}
  uint32_t state=tree_seed?tree_seed:0x6d2b79f5u;auto random=[&](){state^=state<<13;state^=state>>17;state^=state<<5;return state;};
  planted_trees.clear();double d=70+random()%45;
  while(d<length-60){int side=(random()&1)?1:-1;double lateral=side*(400+int(random()%85));int art=int(random()%arcade_trees.size());if(tree_architecture_clear(d,length))planted_trees.push_back({d,lateral,art});d+=65+random()%75;}
 }
 // The native roadside pool is populated before these points become visible.
 // Resolve conflicts only in the unseen approach, then lock each point for
 // every subsequent frame/lap. Never relocate a visible tree.
 for(auto&t:planted_trees)if(!t.settled){
  double gap=phase_difference(t.distance,present_phase());if(gap>118||gap<0)continue;
  bool conflict=false;
  for(unsigned a=profile().props;a<profile().props_end;a+=profile().stride){
   if(!rl(a)||!world_object(a))continue;
   double prop=rw(a+30)-rw(profile().player+30);prop=int16_t(int(prop));
   if(std::abs(prop-gap)<42){conflict=true;break;}
  }
  for(const auto&other:planted_trees)if(&other!=&t&&other.settled&&std::abs(phase_difference(other.distance,t.distance))<56)conflict=true;
  if(conflict){t.distance+=46;if(t.distance>=length-20)t.distance=length-20;}
  if(!tree_architecture_clear(t.distance,length)){t.settled=false;t.distance+=46;if(t.distance>=length-20)t.distance=length-20;continue;}
  if(!conflict)t.settled=true;
 }
}
void roadside_trees(){
 if(!smooth_zoom||!observed_valid||!std::all_of(arcade_trees.begin(),arcade_trees.end(),[](const auto&a){return a.width>0;}))return;
 const char*option=getenv("SMGP_DECOR_TREES");if(option&&option[0]=='0')return;
 plan_trees();if(!game_index&&rw(0x9238)==2)return;
 for(size_t i=0;i<planted_trees.size();i++){
  const auto&t=planted_trees[i];double gap=phase_difference(t.distance,present_phase());if(!t.settled||gap>110||gap+8<=.1)continue;
  const auto&a=arcade_trees[t.art];double native_scale=10/(gap+8),q=std::max(0.0,native_scale*101-6);
  if(precise_center&&!presentation_ray_valid[std::clamp(int(q),0,119)])continue;
  Group g{};g.tree=t.art;g.id=0x10000+int(i);g.full=true;g.z=gap;g.w=a.width;g.h=a.height;g.x=-tree_roots[t.art];g.y=-a.height;
  g.scale=native_scale*210/a.height;g.from_x=present_object_center(q)+t.lateral*native_scale;g.from_foot=present_ground(q);groups.push_back(g);
  if(getenv("SMGP_MOTION_TRACE"))fprintf(stderr,"[SMGP TREE] frame=%u id=%d seed=%08X art=%d course=%.6f lane=%.6f gap=%.6f height=%.6f q=%.4f x=%.4f foot=%.4f\n",tick,g.id,tree_seed,t.art,t.distance,t.lateral,gap,g.h*g.scale,q,g.from_x,g.from_foot);
 }
}
void tree_collision(){
 if(!active||!smooth_zoom||rw(profile().pause)||!observed_valid||(!game_index&&rw(0x9238)==2))return;
 const char*option=getenv("SMGP_DECOR_TREES");if(option&&option[0]=='0')return;
 option=getenv("SMGP_TREE_COLLISIONS");if(option&&option[0]=='0')return;
 if(getenv("SMGP_QA_INPUT_ONLY"))if(const char*after=getenv("SMGP_QA_TREE_COLLISION_AFTER"))if(tick<strtoul(after,nullptr,10))return;
 unsigned broken=game_index?0xfe54:0xfc54,impact=game_index?0xfe5c:0xfc52,impulse=game_index?0xfe56:0xfc5c,side=game_index?0xfe64:0xfc9c;
 unsigned speed=rw(game_index?0xfc2a:0x9108);
 if(!speed||rw(broken)||rw(impact)||rw(impulse)||rw(game_index?0xfe1c:0xfc80))return;
 plan_trees();double phase=rl(profile().player+26)/65536.0,lane=int16_t(rw(profile().player+18));
 double sweep=std::clamp(observed_velocity*4,2.0,12.0);
 for(size_t i=0;i<planted_trees.size();i++){
  auto&t=planted_trees[i];double gap=phase_difference(t.distance,phase),penetration=48-std::abs(lane-t.lateral);
  if(!t.settled||gap>1||gap< -sweep||penetration<=0||(t.hit_at&&tick-t.hit_at<180))continue;
  unsigned force=std::clamp(int(penetration*3),8,160);
  auto write=[](unsigned a,unsigned value){g_ram[a]=value>>8;g_ram[a+1]=value;};
  // The only gameplay addition: the same three collision mailbox fields
  // written by native B290..B2B8 / 15A8C..15AB4. Native handlers own braking,
  // recoil, damage, debris, recovery and audio. No host wheel/speed override.
  write(impact,force);g_ram[side]=lane>t.lateral?255:0;
  bool strong=speed>=(game_index?192u:247u)&&force>56;if(strong)write(broken,1);
  t.hit_at=tick;
  if(getenv("SMGP_MOTION_TRACE"))fprintf(stderr,"[SMGP TREE COLLISION] frame=%u id=%zu lane=%.6f tree=%.6f gap=%.6f force=%u broken=%d speed=%u\n",tick,i,lane,t.lateral,gap,force,strong,speed);
  break;
 }
}
void capture_ending(const GVDP*v){
 bool ended=rw(game_index?0xfe1c:0xfc80)!=0;
 if(!smooth_zoom||!ended){ending_active=false;ending_age=0;ending_banner={};ending_background.clear();return;}
 if(game_index&&!rw(0xfc04)&&shared_gameover.width){
  bool failed_senna=rw(0xfc02)&&rw(0xff5a)>=3&&rw(0xff5a)<16;
  bool retired=rw(0xfe8a)&&rw(0xfe8c)&&(int16_t)rw(0xff5a)<0;
  if(failed_senna||retired){
   if(!ending_active){
    ending_is_retire=retired&&shared_retire.width;ending_banner=ending_is_retire?shared_retire:shared_gameover;ending_age=0;
    // GAME OVER rests where the World cartridge parks it; RETIRE starts centred.
    ending_anchor_x=ending_is_retire?(256-ending_banner.width)/2:28;ending_anchor_y=ending_is_retire?128-ending_banner.height/2:146;
    if(ending_is_retire){ending_background=last_race_background;ending_width=last_race_width;}
   }
   if(ending_is_retire)result_retire=true;
   ending_active=true;
   if(getenv("SMGP_PLAYER_TRACE"))fprintf(stderr,"[SMGP %s] frame=%u glyphs=%d source=%d,%d zoom=continuous freeze=%d game=1 native_failure=1\n",ending_is_retire?"RETIRE":"GAME OVER",tick,ending_is_retire?6:8,ending_banner.width,ending_banner.height,!ending_background.empty());return;
  }
 }
 unsigned base=(v->reg[5]&((v->reg[12]&1)?0x7e:0x7f))<<9,link=0;std::vector<Piece>letters;
 int top=10000,bottom=-10000,left=10000,right=-10000;
 if(getenv("SMGP_LETTER_TRACE")){
  unsigned l2=0;fprintf(stderr,"[SMGP LETTERS] frame=%u age=%u active=%d",tick,ending_age,ending_active);
  for(int n=0;n<14;n++){unsigned a=base+l2*8,attr=word(v,a+4),size=v->vram[(a+2)&65535];
   fprintf(stderr," |%d:%d,%d %dx%d t%u f%u pr%d",l2,(word(v,a+6)&511)-128,(word(v,a)&511)-128,(((size>>2)&3)+1)*8,((size&3)+1)*8,attr&2047,(attr>>11)&3,(attr>>15)&1);
   l2=v->vram[(a+3)&65535]&127;if(!l2)break;}
  fprintf(stderr,"\n");
 }
 // Qualified original large GAME OVER row: eight high-priority 24x24
 // glyph pieces in palette zero, all on the same baseline, inside H32.
 // Read the actual SAT/VRAM, never repurpose ordinary HUD/car glyphs.
 for(int n=0;n<8;n++){
  unsigned a=base+link*8,attr=word(v,a+4),size=v->vram[(a+2)&65535];
  int y=(word(v,a)&511)-128,x=(word(v,a+6)&511)-128;
  int w=(((size>>2)&3)+1)*8,h=((size&3)+1)*8;
  if(!(attr&0x8000)||(attr&0x6000)||h<16||y<80||y>188||x< -16||x>256)return;
  Piece p{};p.sat=link;p.x=x;p.y=y;p.w=w;p.h=h;p.tile=attr&2047;p.pal=0;p.flip=(attr>>11)&3;
  decode_pixels(p,v,0,0,false);letters.push_back(p);left=std::min(left,x);right=std::max(right,x+w);top=std::min(top,y);bottom=std::max(bottom,y+h);
  link=v->vram[(a+3)&65535]&127;if(n<7&&(!link||link>=80))return;
 }
 if(right-left<100||right-left>280||bottom-top>64)return;
 if(!ending_active){
  ending_is_retire=false;
  ending_age=0;ending_anchor_x=left;ending_anchor_y=top;ending_banner.width=right-left;ending_banner.height=bottom-top;ending_banner.pixels.assign(ending_banner.width*ending_banner.height,0);
  for(const auto&p:letters)for(int y=0;y<p.h;y++)for(int x=0;x<p.w;x++)if(auto c=p.pixels[y*p.w+x])ending_banner.pixels[(p.y-top+y)*ending_banner.width+p.x-left+x]=c;
 }
 for(const auto&p:letters)selected[p.sat]=true;
 ending_active=true;
 if(getenv("SMGP_PLAYER_TRACE"))fprintf(stderr,"[SMGP GAME OVER] frame=%u glyphs=8 source=%d,%d zoom=continuous freeze=%d game=%d\n",tick,ending_banner.width,ending_banner.height,!ending_background.empty(),game_index);
}
void ending_overlay(std::vector<uint32_t>&out,int stride,int width,int height){
 if(!ending_active||ending_banner.pixels.empty())return;
 double t=std::clamp(ending_age/90.0,0.0,1.0),ease=t*t*(3-2*t);
 double unit=24.0/ending_banner.height;
 double maximum=ending_is_retire?std::min((height-40.0)/ending_banner.height,(width-20.0)/ending_banner.width):std::min(3.4*unit,(width-20.0)/ending_banner.width);
 // Hand-off from the cartridge: begin at the native size and place (1:1), then grow while the
 // centre glides to the screen centre. Restarting from a small size made the word shrink first.
 double zoom_start=1.0,zoom=zoom_start+(std::max(maximum,zoom_start)-zoom_start)*ease;
 double native_left=(width-256)*.5+ending_anchor_x,centre_x=native_left+ending_banner.width*.5,centre_y=ending_anchor_y+ending_banner.height*.5;
 centre_x+=(width*.5-centre_x)*ease;centre_y+=(128.0-centre_y)*ease;
 double w=ending_banner.width*zoom,h=ending_banner.height*zoom,x0=centre_x-w*.5,y0=centre_y-h*.5;
 for(int y=std::max(0,int(std::floor(y0)));y<std::min(height,int(std::ceil(y0+h)));y++)
  for(int x=std::max(0,int(std::floor(x0)));x<std::min(width,int(std::ceil(x0+w)));x++){
   uint32_t background=out[y*stride+x];unsigned red=0,green=0,blue=0;
   for(int yy=0;yy<3;yy++)for(int xx=0;xx<3;xx++){
    double u=(x+(xx+.5)/3-x0)/zoom,v=(y+(yy+.5)/3-y0)/zoom;
    uint32_t c=background;
    if(u>=0&&v>=0&&u<ending_banner.width&&v<ending_banner.height){auto p=ending_banner.pixels[int(v)*ending_banner.width+int(u)];if(p>>24)c=p;}
    red+=(c>>16)&255;green+=(c>>8)&255;blue+=c&255;
   }
   out[y*stride+x]=0xff000000u|((red/9)<<16)|((green/9)<<8)|(blue/9);
  }
 if(getenv("SMGP_PLAYER_TRACE"))fprintf(stderr,"[SMGP ENDING ZOOM] frame=%u age=%u zoom=%.9f draw=%.9f,%.9f,%.9f,%.9f paused=%d\n",tick,ending_age,zoom,x0,y0,w,h,rw(profile().pause)!=0);
}
uint32_t colour(const GVDP *v,unsigned index){
 if(index==GVDP_WS_BAR_INDEX)return 0xff000000u;
 if(index>=GVDP_HOST_PALETTE_BASE){const uint32_t*p=gvdp_host_palette();return p?p[index-GVDP_HOST_PALETTE_BASE]:0xff000000u;}
 int level=index>=128?GENESIS_DAC_HIGHLIGHT:(index>=64?GENESIS_DAC_SHADOW:GENESIS_DAC_NORMAL);
 return genesis_dac_cram_to_argb(v->cram[index&63],level);
}
unsigned plane_index(const GVDP*v,int x,int y){
 unsigned size=v->reg[16],wt=(size&3)==1?64:((size&3)==3?128:32);
 unsigned ht=((size>>4)&3)==1?64:(((size>>4)&3)==3?128:32);
 x&=wt*8-1;y&=ht*8-1;
 unsigned a=((v->reg[4]&7)<<13)+((y/8)*wt+x/8)*2,attr=word(v,a);
 int sx=(x&7)^((attr&0x800)?7:0),sy=(y&7)^((attr&0x1000)?7:0);
 unsigned b=v->vram[((attr&2047)*32+sy*4+sx/2)&65535],n=(sx&1)?b&15:b>>4;
 return n?((attr>>9)&48)+n:v->reg[7]&63;
}
uint32_t plane_b(const GVDP*v,int x,int y){return colour(v,plane_index(v,x,y));}
unsigned foreground_pattern(const GVDP*v,int x,int line){
 int w=gvdp_screen_width(v),wt=(v->reg[16]&3)==1?64:((v->reg[16]&3)==3?128:32);
 int ht=((v->reg[16]>>4)&3)==1?64:(((v->reg[16]>>4)&3)==3?128:32);
 bool wh=(v->reg[17]&128)?x>=(v->reg[17]&31)*16:x<(v->reg[17]&31)*16;
 bool wv=(v->reg[18]&128)?line>=(v->reg[18]&31)*8:line<(v->reg[18]&31)*8;
 unsigned base;int px=x,py=line;
 if(wh||wv){base=(v->reg[3]&(w==320?60:62))<<10;wt=w==320?64:32;}
 else{
  base=(v->reg[2]&56)<<10;int mode=v->reg[11]&3;
  int row=mode==3?line:(mode==2?line&~7:0);
  px-=int16_t(word(v,((v->reg[13]&63)<<10)+row*4));
  int col=(v->reg[11]&4)?(std::clamp(x,0,w-1)>>4)*2:0;py+=v->vsram[col]&1023;
 }
 px&=wt*8-1;py&=ht*8-1;unsigned attr=word(v,base+(py/8*wt+px/8)*2);
 int sx=(px&7)^((attr&0x800)?7:0),sy=(py&7)^((attr&0x1000)?7:0);
 unsigned value=v->vram[((attr&2047)*32+sy*4+sx/2)&65535],n=(sx&1)?value&15:value>>4;
 return n?attr&2047:~0u;
}
uint32_t panorama(const GVDP*v,int x,int y){
 // $1B26 streams 36 columns from the 1024px source in RAM into a 512px
 // ring. The rest of the VRAM ring belongs to the mirror. Read the source,
 // not the unused ring cells, to expose genuine scenery beyond H32.
 x&=1023;unsigned attr=rw(profile().panomap+((y-40)/8)*384+(x/8)*2);
 int sx=(x&7)^((attr&0x800)?7:0),sy=(y&7)^((attr&0x1000)?7:0);
 unsigned b=v->vram[((attr&2047)*32+sy*4+sx/2)&65535],n=(sx&1)?b&15:b>>4;
 return colour(v,n?((attr>>9)&48)+n:v->reg[7]&63);
}
uint32_t panorama_sky(const GVDP*v){return colour(v,(rw(profile().panomap)>>9)&48);}
uint32_t mix_colour(uint32_t a,uint32_t b,double f){
 unsigned k=(unsigned)std::clamp(f*256.0,0.0,256.0),j=256-k;
 return 0xff000000u|((((a&0xff00ff)*j+(b&0xff00ff)*k)>>8)&0xff00ff)|((((a&0xff00)*j+(b&0xff00)*k)>>8)&0xff00);
}
double projected_q(double y){
 // Search from near to far: a hill crest hides the more distant intersections.
 double last=present_ground(95),near=present_ground(87);
 if(y>last&&last>near)return 95+(y-last)*8/(last-near);
 for(int i=94;i>=0;i--){
  double a=present_ground(i),b=present_ground(i+1);
  if(b>a&&y>=a&&(y<=b||i==94))return i+(y-a)/(b-a);
 }
 return -1;
}
struct RoadSample{double center,scale;int bank;};
RoadSample road_sample(double q,double phase,int bank){
 RoadSample sample{present_center(q),38/(q+6),bank};
 bool inside_tunnel=!game_index&&rw(0x9238)>=2&&rw(0x9238)<=3;
 if(bank>=304&&!rw(profile().special)&&!inside_tunnel){
  // A continuous world UV, calibrated against all 16 native stripe phases.
  // This samples road material, not delayed/blended screen images.
  double z=ray(stripe_distance.data(),96,std::clamp(q,0.0,95.0));
  double p=std::fmod(phase+z,16.0);if(p<0)p+=16;sample.bank=p<8?304:400;
 }
 return sample;
}
int tunnel_material_bank(const GVDP*v,int preferred){
 // Banks are repurposed at entry/exit. Select a currently authored tunnel
 // material, never a fixed bank number or a ground-colour histogram.
 auto valid=[&](int bank){return plane_b(v,0,bank+32)==0xff000000u&&plane_b(v,511,bank+32)==0xff000000u&&plane_b(v,256,bank+32)!=0xff000000u;};
 if(valid(preferred))return preferred;
 for(int bank:{112,208,304,400})if(valid(bank))return bank;
 return preferred;
}
uint32_t road_material(const GVDP*v,double x,const RoadSample&sample){
 double sx=256+(x-sample.center)*sample.scale;int bank=sample.bank;
 if(sx<0||sx>=511)return plane_b(v,0,bank+32);
 int ix=(int)std::floor(sx);return mix_colour(plane_b(v,ix,bank+32),plane_b(v,ix+1,bank+32),sx-ix);
}
// Course material, measured live on the cartridge's own road rows (B atlas,
// kerb-alternating banks 304/400, shoulder row +32): the kerb's outer edge and
// the start of the grass, in road source px from the centre column 256.
// Monaco GP II: kerb 86..105, grass 105; Brazil: kerb 122..141, run-off grey
// 141..190, grass 190; World Monaco: kerb 103..125, grass 125. A single fixed
// kerb (first course) put the arcade stacks on the asphalt of wider courses.
struct CourseMaterial{double kerb_src=105,grass_src=105;};
CourseMaterial course_material(){
 static CourseMaterial cached[2]={{105,105},{125,125}};static unsigned at[2]={~0u,~0u};
 auto&c=cached[game_index?0:1];
 if(at[game_index?0:1]==tick||tunnel_phase()!=0)return c;
 at[game_index?0:1]=tick;
 const GVDP*v=&g_machine.vdp;
 auto kerb_pixel=[](uint32_t p){int r=(p>>16)&255,g=(p>>8)&255,b=p&255;
  return (r>=220&&g>=220&&b>=220)||(r>150&&g<110&&b<110)||(b>150&&r<110&&g<110)||(r>180&&g>150&&b<110);};
 int kerb=-1,grass=-1;
 for(int row:{336,432}){
  int start=-1;
  for(int x=40;x<255;x++){uint32_t p=plane_b(v,256+x,row);int r=(p>>16)&255,g=(p>>8)&255,b=p&255;if(g>r+20&&g>b+20){start=x;break;}}
  if(start<0)continue;
  int last=-1;for(int x=40;x<start;x++)if(kerb_pixel(plane_b(v,256+x,row)))last=x;
  if(last>0){kerb=std::max(kerb,last+1);grass=std::max(grass,start);}
 }
 if(kerb>=60&&kerb<=230&&grass>=kerb){c.kerb_src=kerb;c.grass_src=grass;}
 return c;
}
double kerb_units(){return course_material().kerb_src*101/38;}
double course_grass_src(){return course_material().grass_src;}
// Arcade replacement kind of a cartridge prop mapping: 1 chevron, 2 tyres.
int arcade_prop_kind(unsigned signature){
 const char*option=getenv("SMGP_ARCADE_PROPS");if(option&&option[0]=='0')return 0;
 int kind=game_index?(signature==0x1b24?1:(signature==0x1ecc||signature==0x1f68)?2:0)
                    :(signature==0x11080?1:(signature==0x11452||signature==0x114ee)?2:0);
 auto swept=sweep_props.find(signature|(unsigned(game_index)<<31));
 if(!kind&&swept!=sweep_props.end()&&swept->second.kind<=2)kind=swept->second.kind;
 return kind;
}
// Gap between the kerb and the road-side edge of an arcade board/stack: the
// owner asked for them to start just after the kerb, on the grey run-off.
constexpr double prop_kerb_gap=16;
bool kerb_props_enabled(){const char*o=getenv("SMGP_KERB_PROPS");return !(o&&o[0]=='0');}
// Lateral extent (units, absolute) of an arcade prop placed at the kerb.
bool arcade_prop_extent(int kind,double&inner,double&outer){
 const auto&a=arcade_props21[kind==1?PROP_CHEVRON_BLUE:PROP_TYRES_RED];
 if(!a.art.width||a.unit<=0)return false;
 inner=kerb_units()+prop_kerb_gap;outer=inner+a.art.width*a.unit*(50.0/42.38);return true;
}
void prop_collision(){
 // Gameplay addition requested by the owner (0.21ab): the arcade boards and
 // tyre stacks are wider than the cartridge objects; their drawn extent now
 // collides. Same mailbox as tree_collision (native handlers own the result).
 if(!active||!smooth_zoom||rw(profile().pause)||!observed_valid||!kerb_props_enabled())return;
 if(!game_index&&rw(0x9238)>=1&&rw(0x9238)<=3)return;
 const char*option=getenv("SMGP_PROP_COLLISIONS");if(option&&option[0]=='0')return;
 unsigned broken=game_index?0xfe54:0xfc54,impact=game_index?0xfe5c:0xfc52,impulse=game_index?0xfe56:0xfc5c,side_byte=game_index?0xfe64:0xfc9c;
 unsigned speed=rw(game_index?0xfc2a:0x9108);
 if(!speed||rw(broken)||rw(impact)||rw(impulse)||rw(game_index?0xfe1c:0xfc80))return;
 const auto&pr=profile();double lane=int16_t(rw(pr.player+18)),sweep=std::clamp(observed_velocity*4,2.0,12.0);
 static std::map<unsigned,unsigned> hit_at;
 for(unsigned a=pr.props;a<pr.props_end;a+=pr.stride){
  if(!rl(a)||!world_object(a)||tunnel_fitting(a))continue;
  unsigned family=rl(a+8);if(family<0x200||family+36>=rom_limit())continue;
  int kind=arcade_prop_kind(roml(family+32));if(!kind)continue;
  double gap=(int16_t)(rw(a+30)-rw(pr.player+30))-(rl(pr.player+30)&65535)/65536.0;
  if(gap>1||gap< -sweep)continue;
  double inner,outer;if(!arcade_prop_extent(kind,inner,outer))continue;
  int side=(int16_t)rw(a+18)<0?-1:1;double l=side*lane,half=40;
  double penetration=std::min(l+half-inner,outer-(l-half));
  if(penetration<=0)continue;
  auto last=hit_at.find(a);if(last!=hit_at.end()&&tick-last->second<180)continue;
  unsigned force=std::clamp(int(std::min(penetration,48.0)*3),8,160);
  auto write=[](unsigned at,unsigned value){g_ram[at]=value>>8;g_ram[at+1]=value;};
  write(impact,force);g_ram[side_byte]=lane>side*(inner+outer)*.5?255:0;
  bool strong=speed>=(game_index?192u:247u)&&force>56;if(strong)write(broken,1);
  hit_at[a]=tick;
  if(getenv("SMGP_MOTION_TRACE"))fprintf(stderr,"[SMGP PROP COLLISION] frame=%u object=%04X kind=%d lane=%.1f inner=%.1f outer=%.1f gap=%.3f force=%u broken=%d speed=%u\n",tick,a,kind,lane,side*inner,side*outer,gap,force,strong,speed);
  break;
 }
}
// Owner 0.21ab: continuous, imposing zoom of the GP II flag marshal, keeping
// his original animation. The cartridge's family table (ROM 9E8) is one
// ladder in which the pose and the size change together (maps 32B2..334C:
// flag sideways, up, down, raised high); it switches every few updates and
// drops him ~7 units before the line. Here the CURRENT native pose is drawn,
// resized so that his body (red suit + yellow hair, flag excluded) follows the
// common projection continuously; after the cartridge drops him at the line
// the ladder continues (3326, then 334C flag raised) until he leaves the frame
// past the camera. Body height = native 3306 body at its own distance (gap
// 7.05). Ground anchored at the finish line (course phase 0), native lateral.
// Read-only; SMGP_FLAGMAN=0 keeps the cartridge.
void flagman_group(const GVDP*v){
 const unsigned a=flagman_object();
 if(!flagman_actor(a))return;
 unsigned family=rl(a+8);if(family<0x200||family+36>=rom_limit())return;
 std::array<unsigned,9> ladder;for(int i=0;i<9;i++){ladder[i]=roml(family+i*4);if(ladder[i]<0x200||ladder[i]+2>=rom_limit())return;}
 static std::map<unsigned,int> body_rows;
 double gap=phase_difference(0,present_phase());
 unsigned native=rl(a+4),state=rw(a+14);
 bool shown=state!=0xffff&&!(state&0x8000)&&std::find(ladder.begin(),ladder.end(),native)!=ladder.end();
 static unsigned last[2]={0,0};unsigned&previous=last[game_index?1:0];
 if(std::find(ladder.begin(),ladder.end(),previous)==ladder.end())previous=ladder[6];
 unsigned mapping=shown?native:(gap<-1.5?ladder[8]:gap<1?ladder[7]:previous);
 if(shown)previous=native;
 const int tile=game_index?0:(int16_t)rw(a+12);
 auto decode=[&](unsigned m,FlagmanArt&art)->bool{
  ObjectPose pose{};pose.mapping=m;pose.tile=tile;unsigned count=romw(m);if(count>16)return false;
  std::vector<Piece> parts;int x0=1e9,y0=1e9,x1=-1e9,y1=-1e9;
  for(unsigned i=0;i<=count;i++){Piece p{};if(!mapping_piece(p,pose,i))return false;decode_pixels(p,v,m,i,false);
   x0=std::min(x0,p.x);y0=std::min(y0,p.y);x1=std::max(x1,p.x+p.w);y1=std::max(y1,p.y+p.h);parts.push_back(p);}
  if(x1<=x0||y1<=y0)return false;
  art.width=x1-x0;art.height=y1-y0;art.left=x0;art.top=y0;art.pixels.assign(size_t(art.width)*art.height,0);
  for(const auto&p:parts)for(int y=0;y<p.h;y++)for(int x=0;x<p.w;x++)if(auto c=p.pixels[y*p.w+x])
   art.pixels[size_t(p.y-y0+y)*art.width+p.x-x0+x]=c;
  return true;
 };
 // Body rows of a pose: from the topmost red/yellow texel to the feet.
 auto body=[&](unsigned m,const FlagmanArt&art){
  auto found=body_rows.find(m);if(found!=body_rows.end())return found->second;
  int top=-1;for(int y=0;y<art.height&&top<0;y++)for(int x=0;x<art.width;x++){uint32_t p=art.pixels[size_t(y)*art.width+x];if(!(p>>24))continue;
   int r=(p>>16)&255,g=(p>>8)&255,b=p&255;if((r>g+50&&r>b+50)||(r>150&&g>120&&b<100)){top=y;break;}}
  int rows=top<0?art.height:art.height-top;
  if(scene_fade(&g_machine.vdp)==255&&top>=0)body_rows[m]=rows;
  return rows;
 };
 if(!decode(mapping,flagman_art))return;
 FlagmanArt reference;double reference_body=36;
 if(decode(ladder[6],reference))reference_body=body(ladder[6],reference);
 int pose_body=std::max(4,body(mapping,flagman_art));
 Group g{};g.id=int(a);g.flagman=true;g.full=true;g.z=gap;
 if(gap>170||gap+8<=.1){g.culled=true;groups.push_back(g);return;}
 // Native 3306 is drawn 1:1 at gap 7.05 (scale 10/15.05): its body there
 // defines the marshal's world height; every pose is resized to that body.
 const double world_body=reference_body*15.05/10;
 g.scale=10/(gap+8)*world_body/pose_body;g.w=flagman_art.width;g.h=flagman_art.height;g.x=flagman_art.left;g.y=flagman_art.top;
 double native_scale=10/(gap+8),q=std::max(0.0,native_scale*101-6),lateral=(int16_t)rw(a+18);
 g.road_x=present_object_center(std::min(q,119.0));g.from_x=g.road_x+lateral*native_scale;g.from_foot=present_ground(q);
 groups.push_back(g);
 if(getenv("SMGP_MOTION_TRACE"))fprintf(stderr,"[SMGP FLAGMAN] frame=%u gap=%.4f scale=%.4f x=%.2f foot=%.2f size=%dx%d pose=%X native_map=%X body=%d\n",tick,gap,g.scale,g.from_x,g.from_foot,flagman_art.width,flagman_art.height,mapping,native,pose_body);
}
// Far end of the road (owner 0.21ab, corrected): the cartridge raster ends the
// road a few lines below the horizon and shows the panorama's grass strip
// there, so distant cars stood on grass. After all lines of the frame are
// built, continue the TOPMOST drawn road line upward with its own perspective
// (q step and centre step of the top drawn lines), only when that line is the
// far end of the course table (q <= 8), not a crest, never in a tunnel, and
// only over road/kerb/run-off material. The first version re-projected each
// line through the ground table; at crests and flat far rows the table maps
// many lines to one depth and drew wide grey blocks above the road.
void extend_far_road(int width,int offset){
 // Opt-in only (QA): continuing the road above the native top paints asphalt
 // over the horizon/panorama; the native road drawing stays authoritative.
 const char*option=getenv("SMGP_FAR_ROAD");if(!option||option[0]!='1')return;
 if(!smooth_zoom||road_pixels.size()!=(size_t)width*224||tunnel_phase()!=0)return;
 int top=-1;for(int y=65;y<221;y++)if(road_rows[y].valid&&road_rows[y].road){top=y;break;}
 if(top<0||!road_rows[top+1].road||!road_rows[top+2].road)return;
 double q0=road_rows[top].road_q,q2=road_rows[top+2].road_q,dq=(q2-q0)/2;
 if(q0<0||q0>8||dq<=.05||dq>3)return;
 double c0=road_rows[top].road_center,dc=(road_rows[top+2].road_center-c0)/2;
 if(std::abs(dc)>8)return;
 const GVDP*v=&g_machine.vdp;double grass=course_material().grass_src;
 for(int k=1;k<=12;k++){
  int y=top-k;if(y<65||!road_rows[y].valid||road_rows[y].road)break;
  double q=q0-k*dq;if(q<=-4.5)break;
  RoadSample s=road_sample(std::max(q,0.0),present_phase(),road_rows[top].road_bank);
  s.center=c0-k*dc;s.scale=38/(q+6);
  for(int x=0;x<width;x++){
   double sx=256+(x-offset+.5-s.center)*s.scale;if(std::abs(sx-256)>=grass)continue;
   road_pixels[size_t(y)*width+x]=road_material(v,x-offset+.5,s);
  }
 }
}
unsigned ground_index(const GVDP*v,int y){
 unsigned wt=(v->reg[16]&3)==1?64:((v->reg[16]&3)==3?128:32);
 unsigned base=(v->reg[4]&7)<<13;
 return ((word(v,base+((y&511)/8)*wt*2)>>9)&48)|11;
}
void assemble(const GVDP *v){
 prepared=true;prepared_tick=tick;pieces.clear();groups.clear();selected.fill(false);exit_backdrop_actors.clear();
 native_width=gvdp_screen_width(v);
 upload_matched=acquire_uploaded_scene(v);
 prepare_centers();
 if(upload_matched)source_groups(v);
 if(upload_matched)roadside_trees();
 if(upload_matched)flagman_group(v);
 unsigned base=(v->reg[5]&((v->reg[12]&1)?0x7e:0x7f))<<9;
 int link=0;player_slots=0;
 for(int n=0;n<80;n++){
  unsigned a=base+link*8,sz=v->vram[(a+2)&65535],attr=word(v,a+4);
  Piece p{};p.x=(word(v,a+6)&511)-128;p.y=(word(v,a)&511)-128;
  p.w=(((sz>>2)&3)+1)*8;p.h=((sz&3)+1)*8;p.tile=attr&2047;p.pal=(attr>>13)&3;p.flip=(attr>>11)&3;
  unsigned actor=displayed_owner[link];
  selected[link]=actor&&((player_actor(actor)&&player_arcade())||std::any_of(groups.begin(),groups.end(),[&](const Group&g){return g.id==(int)actor;}));
  // Normal cockpit hands have a separate, qualified pattern allocation from
  // the tachometer. Their replacement does not need the first world SAT
  // transfer to finish: otherwise the old cockpit flashed for seven ticks.
  if(player_arcade()&&player_hand_pattern(p.tile,p.pal))selected[link]=true;
  if(selected[link]&&player_actor(actor))++player_slots;
  if(actor&&!selected[link]){
   ++unsupported_slots;
   if(getenv("SMGP_PROJECTION_TRACE"))fprintf(stderr,"[SMGP UNSUPPORTED] frame=%u object=%04X routine=%X family=%X slot=%d removal_phase=%.6f present=%.6f\n",tick,actor,rl(actor),rl(actor+8),link,removal_phase,present_phase());
  }
  int next=v->vram[(a+3)&65535]&127;if(!next||next>=80)break;link=next;
 }
 capture_ending(v);
 history=groups;
 if(getenv("SMGP_MATCH_TRACE"))fprintf(stderr,"[SMGP MATCH] frame=%u generation=%u world=%zu selected=%zu uploaded=%d\n",tick,world_serial,world_objects.size(),std::count(selected.begin(),selected.end(),true),upload_matched);
}
int transform(void*,const GVDP*v,unsigned sat,double bounds[4]){
 if(!active)return 0;
 if(!prepared)assemble(v);
 if(sat>=80||!selected[sat])return 0;
 // Suppress in the visual pass only; the GPU draws the complete source pose.
 bounds[0]=bounds[1]=bounds[2]=bounds[3]=0;return 1;
}
void audit(void*,const GVDP*v,const GVDPVisualAuditRow*r){
 formation_row.fill(0);formation_line=r->line;
 if(formation_active&&gvdp_display_enabled(v)){
  if(game_index){
   if(r->line==0){
    formation_displayed.fill(-1);
    unsigned base=(v->reg[5]&((v->reg[12]&1)?0x7e:0x7f))<<9;
    for(auto f=formation_queue.rbegin();f!=formation_queue.rend();++f){
     bool match=true;std::array<bool,80>seen{};unsigned slot=0;
     for(unsigned n=0;n<80;n++){
      if(slot>=80||seen[slot]){match=false;break;}seen[slot]=true;
      for(unsigned b=0;b<8;b++)if(v->vram[(base+slot*8+b)&65535]!=f->sat[slot*8+b])match=false;
      unsigned next=f->sat[slot*8+3]&127;if(!next)break;slot=next;
     }
     if(match){formation_displayed=f->teams;break;}
    }
   }
   for(int x=0;x<r->total&&x<1024;x++){
    unsigned owner=r->owner[x],index=r->index[x],nibble=index&15;
    if(!owner||owner>80||nibble<6||nibble>9)continue;
    int team=formation_displayed[owner-1];if(team<0)continue;
    // The overhead car's nibble 4 is grey, not its fade reference.
    unsigned white=scene_fade(v);
    int level=index>=128?GENESIS_DAC_HIGHLIGHT:index>=64?GENESIS_DAC_SHADOW:GENESIS_DAC_NORMAL;
    auto paint=team_paint(team,white,level);
    formation_row[x]=paint[nibble==6?2:nibble==7?3:nibble==8?0:1];
   }
  }else{
   // World lists the qualifying order without car icons. Add a compact
   // overhead identity beside each existing row, without moving its text.
   unsigned count=rw(0xff30)==14?15:14,white=scene_fade(v);
   for(unsigned rank=0;rank<count;rank++){
    int y=r->line-(54+int(rank%8)*16);if(y<0||y>=12)continue;
    int x0=r->offset+(rank<8?142:270),team=team_for_grid_rank(rank);
    auto paint=team_paint(team,white);
    for(int x=0;x<8;x++){
     unsigned nibble=formation_car[y*16+x];if(!nibble||x0+x>=r->total)continue;
     formation_row[x0+x]=nibble>=6&&nibble<=9?paint[nibble==6?2:nibble==7?3:nibble==8?0:1]:nibble==4?paint_mix(0xff000000u,0xffffffffu,white):0xff000000u;
    }
   }
  }
 }
 const char *layer_frame=getenv("SMGP_LAYER_FRAME");
 if(layer_frame&&tick==(unsigned)strtoul(layer_frame,nullptr,10)&&r->native_width==256){
  if(layer_native.size()!=256*224){layer_native.resize(256*224);layer_b.resize(256*224);}
  unsigned hb=(v->reg[13]&63)<<10;
  int hs=(int16_t)word(v,hb+r->line*4+2);
  layer_diff[r->line]=0;
  for(int x=0;x<256;x++){
   unsigned b=plane_index(v,x-hs,r->line+v->vsram[1]);
   layer_native[r->line*256+x]=colour(v,r->native_index[x]);
   layer_b[r->line*256+x]=colour(v,b);
   if(x>=56&&x<192&&!r->native_owner[x]&&b!=r->native_index[x])++layer_diff[r->line];
  }
  if(r->line%8==0)fprintf(stderr,"[SMGP LAYERS] frame=%u row=%d hs=%d va=%u vb=%u different=%u\n",tick,r->line,hs,v->vsram[0],v->vsram[1],layer_diff[r->line]);
 }
 const char *row_trace=getenv("SMGP_RASTER_TRACE");
 if(row_trace&&tick==(unsigned)strtoul(row_trace,nullptr,10)&&r->line>=0&&r->line<224){
  unsigned hb=(v->reg[13]&63)<<10;
  fprintf(stderr,"[SMGP ROW] y=%d hs=%d vs=%d srcy=%d left=%d right=%d\n",r->line,(int16_t)word(v,hb+r->line*4+2),(int16_t)v->vsram[1],(r->line+v->vsram[1])&511,r->native_index[0],r->native_index[255]);
 }
 if(!active||r->line<0||r->line>=224)return;
 if(native_frame.size()!=256*224)native_frame.resize(256*224);
 for(int x=0;x<256;x++)native_frame[r->line*256+x]=colour(v,r->native_index[x]);
 if((int)foreground.size()!=r->total*224)foreground.assign(r->total*224,0);
 if((int)tunnel_occlusion.size()!=r->total*224)tunnel_occlusion.assign(r->total*224,0);
 if((int)portal_surface.size()!=r->total*224)portal_surface.assign(r->total*224,0);
 offset_width=r->offset;
 if(!gvdp_display_enabled(v)){
  // SMGP uses real raster display blanking as a tunnel ceiling mask.
  // Reconstructing B or drawing GPU actors over it reveals storage/sky.
  uint32_t backdrop=colour(v,v->reg[7]&63);
  facade_edge[r->line]={false,false};
  if(road_pixels.size()!=(size_t)r->total*224)road_pixels.assign(r->total*224,0);
  road_rows[r->line].valid=true;road_rows[r->line].road=false;
  // Widescreen margins of a blanked raster line: the VDP backdrop is a flat
  // grey band (206,206,206) at the tunnel portal. Continue the native edge
  // material of the same raster line instead, as the tunnel ceiling does.
  const bool margin_scenery=smooth_zoom&&game_index&&tunnel_phase()>=1&&tunnel_phase()<=3&&r->line>=65;
  // GP II tunnel: the blanked rows 65-99 inside the native area are the roof
  // of the tunnel mouth. The VDP backdrop painted them as the flat light grey
  // band the owner rejected (quadrado claro). Use the tunnel's black roof.
  const bool roof_black=margin_scenery&&r->line<100;
  // 0.21ag (owner: black line at the exit): outside the tunnel the blanked
  // roof rows that linger for a few frames are sky, not a black band.
  const bool exit_sky=smooth_zoom&&tunnel_phase()==0&&last_tunnel_tick&&tick-last_tunnel_tick<=60&&r->line>=65&&r->line<130;
  if(exit_sky){
   uint32_t sky=panorama_sky(v);
   for(int x=0;x<r->total;x++){road_pixels[r->line*r->total+x]=sky;foreground[r->line*r->total+x]=0;tunnel_occlusion[r->line*r->total+x]=0;}
   return;
  }
  for(int x=0;x<r->total;x++){
   road_pixels[r->line*r->total+x]=roof_black?0xff000000u:backdrop;
   if(margin_scenery&&(x<r->offset||x>=r->offset+r->native_width)){
    int nx=std::clamp(x<r->offset?0:r->native_width-1,0,r->native_width-1);
    unsigned hb=(v->reg[13]&63)<<10;int hs=(int16_t)word(v,hb+r->line*4+2);
    road_pixels[r->line*r->total+x]=plane_b(v,nx-hs,r->line+v->vsram[1]);
   }
   bool inside=tunnel_phase()>=1&&tunnel_phase()<=3&&r->line>=65;
   tunnel_occlusion[r->line*r->total+x]=inside;
   const bool margin_cell=margin_scenery&&(x<r->offset||x>=r->offset+r->native_width);
   foreground[r->line*r->total+x]=inside||margin_cell||roof_black?0:((r->line>=65||(x>=r->offset&&x<r->offset+256))?backdrop:0);
  }
  return;
 }
 if(!prepared)assemble(v);
 // Continuous tunnel veil (0 open .. 1 inside) from the real distance to the
 // portal. Margins used to flip between scenery and black in one frame at
 // entrance/exit; now they fade with the approach. Exit side (phase 3) must
 // render real scenery in the margins while the veil is still lifting.
 const double veil=smooth_zoom?tunnel_veil():(tunnel_phase()>=2&&tunnel_phase()<=3?1.0:0.0);
 const bool margin_open=smooth_zoom&&tunnel_phase()==3&&veil<.999;
 if(r->line==100&&getenv("SMGP_VEIL_TRACE"))fprintf(stderr,"[SMGP VEIL] frame=%u phase=%u present=%.2f veil=%.3f spans=%zu open=%d\n",tick,tunnel_phase(),present_phase(),veil,tunnel_spans().size(),margin_open?1:0);
 if(smooth_zoom&&tunnel_phase()==1&&r->line>=65){
  unsigned hb=(v->reg[13]&63)<<10;int hs=(int16_t)word(v,hb+r->line*4+2),sy=(r->line+v->vsram[1])&511;
  // The entrance facade is authored by the B raster as well as the SAT.
  // Its upper/lower material rows are not panorama or road. Keep the solid
  // jamb on that actual surface: enlarging a white SAT rectangle into the
  // sky detached it from the mouth, even with perfectly monotonic scale.
  if(sy<40||sy>=496)for(int x=0;x<r->total;x++)
   portal_surface[r->line*r->total+x]=plane_b(v,std::clamp(x-r->offset,0,r->native_width-1)-hs,r->line+v->vsram[1]);
 }
 for(int x=0;x<r->native_width;x++){unsigned owner=r->native_owner[x];if(owner&&owner<=80&&selected[owner-1])++original_world_pixels;}
 for(int x=0;x<r->total;x++){unsigned owner=r->owner[x];if(owner&&owner<=80&&selected[owner-1])++leaked_world_pixels;}
 bool exit_open[2]={false,false};
 {
  auto&row=road_rows[r->line];
  unsigned hb=(v->reg[13]&63)<<10;
  int hs=(int16_t)word(v,hb+r->line*4+2),sy=(r->line+v->vsram[1])&511;
  bool pano=sy>=40&&sy<112;
  if(tunnel_phase()!=0)last_tunnel_tick=tick;
  // Authored facade at this line's native edges (B raster, not HUD/SAT).
  for(int side=0;side<2;side++){
   int grey=0;for(int k=0;k<4;k++)grey+=gp2_facade(plane_b(v,(side?r->native_width-1-k:k)-hs,r->line+v->vsram[1]));
   facade_edge[r->line][side]=game_index&&grey>=3;
  }
  // GP II leaves the tunnel (phase 0) while the authored wall above the exit
  // is still on screen for a frame or two: whole black B rows at the top.
  // Painting them as open sky/panorama made the exit snap from a small
  // opening to a full frame. Keep that lintel (and its margins) black.
  // 0.21ag (owner: black line at the exit, like the grey one at the entrance):
  // the lintel and the margin wall after the exit are off; SMGP_GP2_EXIT_LINTEL=1
  // restores them.
  static const bool exit_lintel=[]{const char*o=getenv("SMGP_GP2_EXIT_LINTEL");return o&&o[0]=='1';}();
  bool lintel=exit_lintel&&game_index&&smooth_zoom&&r->line>=65&&r->line<200&&(sy<112)&&tunnel_phase()==0&&tick-last_tunnel_tick<=30&&gp2_black_row(v,r->line,hs,r->native_width);
  if(lintel)pano=false;
  // World: its sky colour is learned from ordinary open-air rows (the
  // panorama sky index is not valid there) and reused right after an exit.
  static uint32_t world_sky=0;
  bool recent_exit=last_tunnel_tick&&tick-last_tunnel_tick<=60;
  if(!game_index&&r->line>=65&&r->line<80&&sy<40&&tunnel_phase()==0&&!recent_exit){uint32_t c=plane_b(v,r->native_width/2-hs,r->line+v->vsram[1]);if(c&0xffffff)world_sky=c;}
  bool upper_sky=smooth_zoom&&r->line>=65&&sy<40&&tunnel_phase()==0&&!lintel&&
   (game_index||(recent_exit&&world_sky));
  bool flat_ground=game_index&&r->line>=65&&sy>=496&&tunnel_phase()==0;
  // 0.21ag: just after a tunnel exit the cartridge still holds whole black
  // panorama rows (wall above the exit): a black band across the top for a
  // few frames. Outside the tunnel it is sky.
  const bool exit_black_pano=smooth_zoom&&pano&&!lintel&&tunnel_phase()==0&&last_tunnel_tick&&tick-last_tunnel_tick<=60&&gp2_black_row(v,r->line,hs,r->native_width);
  row.road=false;
  row.valid=r->line>=65&&r->line<224&&((sy>=40&&sy<496)||upper_sky||flat_ground||lintel);
  double continuous_q=upload_matched?projected_q(r->line+.5):-1;
  bool continuous_road=continuous_q>=0&&sy>=112&&sy<496;
  // Both cartridges expose a 0..3 tunnel phase (World 9238 / GP II FC84).
  // The raster/margin logic is shared; only the material-bank probes below
  // are World-specific and keep their original gate.
  const unsigned tphase=tunnel_phase();
  bool tunnel=tphase>=1&&tphase<=3;
  bool wtunnel=tunnel&&!game_index;
  bool ceiling=tunnel&&r->line>=65&&(pano||sy<40||sy>=496||continuous_q<0);
  if(ceiling)row.valid=true;
  if(row.valid){
   if(road_pixels.size()!=(size_t)r->total*224)road_pixels.assign(r->total*224,0);
   std::array<RoadSample,3> samples,tunnel_samples;
   if(continuous_road)for(int sub=0;sub<3;sub++){
    double pq=projected_q(r->line+(sub+.5)/3.0);
    // Rows hidden by the original cockpit are storage for the next road
    // strip. Inside the tunnel they must continue its black shoulders.
    int bank=sy-(sy-112)%96;
    if(wtunnel&&tphase>=2&&r->line>=200)bank=tunnel_material_bank(v,bank);
    samples[sub]=road_sample(pq>=0?pq:continuous_q,present_phase(),bank);
    tunnel_samples[sub]=samples[sub];
    if(wtunnel&&tphase>=2)tunnel_samples[sub].bank=tunnel_material_bank(v,bank);
   }
   if(continuous_road&&r->line==190&&getenv("SMGP_MOTION_TRACE"))fprintf(stderr,"[SMGP ROAD WORLD] frame=%u q=%.6f center=%.6f phase=%.6f camera_age=%.2f\n",tick,continuous_q,samples[1].center,present_phase(),camera_age());
   exit_lintel_sky[r->line]=false;
   if(game_index&&smooth_zoom&&tphase==3&&r->line>=65&&r->line<200){
    // Raster lines arrive top to bottom. Once the opening reaches an edge on a
    // panorama row, every lower line of this frame is outside on that side:
    // the B raster has a one-line black seam where the panorama meets the
    // road, which must not close the margin again under the horizon.
    static unsigned exit_frame=~0u;static bool exit_seen[2];
    if(exit_frame!=tick){exit_frame=tick;exit_seen[0]=exit_seen[1]=false;}
    // 0.21ag: the last phase-3 instant leaves a full-width black band (wall
    // above the exit) across the top. When the opening below already reaches
    // both native edges, those rows are sky.
    if(gp2_black_row(v,r->line,hs,r->native_width)){
     // Only when the opening is full width: every panorama row below is open
     // at both native edges (not just one row reaching them).
     int rows=0,open=0;
     for(int y=r->line+1;y<200;y++){
      int ysy=(y+v->vsram[1])&511;if(ysy<40||ysy>=112)continue;
      if(gp2_black_row(v,y,(int16_t)word(v,((v->reg[13]&63)<<10)+y*4+2),r->native_width))continue;
      // Finished native image (sprites included) of the previous frame: the
      // cartridge's black frame narrows the B opening.
      int lit=0;
      if(native_frame.size()==256*224)for(int k=0;k<3;k++){
       lit+=(native_frame[y*256+k]&0xffffff)!=0;
       lit+=(native_frame[y*256+r->native_width-1-k]&0xffffff)!=0;
      }
      ++rows;open+=lit>=5&&gp2_exit_open(v,y,0,r->native_width)&&gp2_exit_open(v,y,1,r->native_width);
     }
     exit_lintel_sky[r->line]=rows>=4&&open*10>=rows*9;
     if(exit_lintel_sky[r->line])exit_seen[0]=exit_seen[1]=true;
    }else exit_lintel_sky[r->line]=false;
    for(int side=0;side<2;side++){
     if(!exit_seen[side]){
      bool open=gp2_exit_open(v,r->line,side,r->native_width);
      // 0.21af: with the cartridge's own exit frame (gp2_native_exit), its
      // black SAT frame covers the B opening; the margin opens only where the
      // finished native edge (sprites included) is open as well.
      if(open&&gp2_native_exit()){int lit=0;for(int k=0;k<4;k++)lit+=(colour(v,r->native_index[side?r->native_width-1-k:k])&0xffffff)!=0;open=lit>=3;}
      exit_seen[side]=open;
     }
     exit_open[side]=exit_seen[side];
    }
   }
   row.road=continuous_road&&!tunnel&&!lintel;
   if(row.road){row.road_q=projected_q(r->line+.5);row.road_center=samples[1].center;row.road_bank=samples[1].bank;}
   for(int xo=0;xo<r->total;xo++){
    if(lintel){road_pixels[r->line*r->total+xo]=0xff000000u;continue;}
    if(exit_lintel_sky[r->line]){road_pixels[r->line*r->total+xo]=panorama_sky(v);continue;}
    if(upper_sky){road_pixels[r->line*r->total+xo]=game_index?panorama_sky(v):world_sky;continue;}
    const bool outside=xo<r->offset||xo>=r->offset+r->native_width;
    const bool opened=outside&&(margin_open||exit_open[xo>=r->offset+r->native_width]);
    if(opened&&exit_open[xo>=r->offset+r->native_width]&&!pano){
     // Exit opening beyond the native edge: sky above the panorama, the
     // authored B edge column below it (the outside ground). The road rows
     // still hold the tunnel's black shoulder material in this phase, which
     // left a black block in the lower margins of a full-width opening.
     road_pixels[r->line*r->total+xo]=sy<40?panorama_sky(v):plane_b(v,std::clamp(xo-r->offset,0,r->native_width-1)-hs,r->line+v->vsram[1]);continue;
    }
    if(flat_ground){
     // GP II's hill filler is atlas row 509, sampled in its authored native
     // span. Hidden columns use another palette: modulo/edge-atlas fetches
     // made a white rectangle (510) or blue rectangle (509) in wide output.
     uint32_t ground=plane_b(v,std::clamp(xo-r->offset,0,r->native_width-1)-hs,r->line+v->vsram[1]);
     if(outside&&smooth_zoom&&tick-last_tunnel_tick>30)ground=gp2_entrance_margin(v,r->line,xo>=r->offset,r->native_width,ground);
     road_pixels[r->line*r->total+xo]=ground;continue;
    }
    if(tunnel&&tphase>=2&&r->line<200&&xo>=r->offset&&xo<r->offset+r->native_width){
     // A tunnel row can contain both the exit aperture and black wall in
     // the same material bank. Reprojecting its green shoulder through
     // the wall makes a horizontal green bar beside the exit. Preserve
     // the authored B-plane wall footprint, without copying SAT or HUD.
     uint32_t native_b=plane_b(v,xo-r->offset-hs,r->line+v->vsram[1]);
     // GP II props/pillars are drawn over this wall; its occlusion mask
     // would erase them (the World treats them as architecture instead).
     if(native_b==0xff000000u){road_pixels[r->line*r->total+xo]=native_b;if(!game_index)tunnel_occlusion[r->line*r->total+xo]=1;continue;}
    }
    if(ceiling&&!opened){
     int native_x=xo-r->offset;
     // Tunnel roof/storage rows have only the native authored columns.
     // Their edge material continues outside the portal; the rest of the
     // VRAM ring contains mirror/HUD storage and is never scenery.
     if(native_x<0||native_x>=r->native_width){
      auto edge_at=[&](int line){
       if(line<0||line>=224)return 0u;
       int lhs=(int16_t)word(v,hb+line*4+2);
       return plane_b(v,(native_x<0?0:r->native_width-1)-lhs,line+v->vsram[1]);
      };
      uint32_t edge=edge_at(r->line);
      // Owner 0.21ac (World entrance, white line blinking in the margin): a
      // thin diagonal (kerb highlight) crosses the native edge column on
      // one or two rows only; repeating that single pixel across the whole
      // margin drew a full-width white line that flickered as it moved.
      // A feature 1-2 rows thin at the edge takes its vertical neighbours'
      // surface instead. Thicker surfaces (facade, wall) are unchanged.
      if(tphase==1){
       uint32_t up=edge_at(r->line-1),dn=edge_at(r->line+1),dn2=edge_at(r->line+2),up2=edge_at(r->line-2);
       if(edge!=up&&edge!=dn&&up==dn)edge=up;
       else if(edge!=up&&edge==dn&&edge!=dn2&&up==dn2)edge=up;
       else if(edge!=dn&&edge==up&&edge!=up2&&dn==up2)edge=dn;
      }
      if(tphase==1&&pano&&edge!=0xff000000u){
       double source=native_x-(upload_matched?present_panorama():hs);int ix=(int)std::floor(source);
       road_pixels[r->line*r->total+xo]=mix_colour(panorama(v,ix,sy),panorama(v,ix+1,sy),source-ix);
      }else{
       if(game_index&&tphase==1)edge=gp2_entrance_margin(v,r->line,native_x>=0,r->native_width,edge);
       road_pixels[r->line*r->total+xo]=tphase==1||(game_index&&gp2_facade(edge))?edge:0xff000000u;
      }
     }else road_pixels[r->line*r->total+xo]=plane_b(v,native_x-hs,r->line+v->vsram[1]);continue;
    }
    if(pano&&exit_black_pano){road_pixels[r->line*r->total+xo]=panorama_sky(v);continue;}
    if(pano){
     double source=xo-r->offset-(upload_matched?present_panorama():hs);
     int ix=(int)std::floor(source);
     road_pixels[r->line*r->total+xo]=mix_colour(panorama(v,ix,sy),panorama(v,ix+1,sy),source-ix);continue;
    }
    // The bitmap's near right half is storage, not a finite world edge.
    // Row 32 contains BOTH authored shoulders. Unproject that material row
    // with the road ray's true scale and sample only its valid extent.
    int q=(sy-112)%96,bank=sy-q;
    if(continuous_road){
     uint32_t pixels[3];
     for(int sub=0;sub<3;sub++){
      const auto&material=wtunnel&&tphase>=2&&(xo<r->offset||xo>=r->offset+r->native_width)?tunnel_samples[sub]:samples[sub];
      pixels[sub]=road_material(v,xo-r->offset+.5,material);
     }
     unsigned rr=0,gg=0,bb=0;for(auto p:pixels){rr+=(p>>16)&255;gg+=(p>>8)&255;bb+=p&255;}
     road_pixels[r->line*r->total+xo]=0xff000000u|((rr/3)<<16)|((gg/3)<<8)|(bb/3);continue;
    }
    double scale=(q+6.0)/38.0;
    double source=256.0+(xo-r->offset-(256+hs))/scale;
    int sx=(int)std::floor(source);
    road_pixels[r->line*r->total+xo]=plane_b(v,sx>=0&&sx<512?sx:0,bank+32);
   }
   // 0.21af: entrance margins never carry the facade's partial black base
   // as long bars, whatever filled them (road material, B edge, ceiling).
   if(game_index&&smooth_zoom&&r->line>=65&&r->line<200&&(tphase==1||(tphase==0&&tick-last_tunnel_tick>30))){
    for(int side=0;side<2;side++){
     uint32_t fill=gp2_entrance_margin(v,r->line,side,r->native_width,0xff000000u);
     if(fill==0xff000000u)continue;
     // Same base inside the native columns, from the edge inward: black
     // directly under (already processed) wall grey. The tunnel mouth is black
     // above as well and is never touched.
     if(r->line>=174)for(int k=0;k<r->native_width/2;k++){
      int x=r->offset+(side?r->native_width-1-k:k);
      uint32_t&p=road_pixels[r->line*r->total+x];
      if((p&0xffffff)!=0||!gp2_facade(road_pixels[(r->line-1)*r->total+x]))break;
      p=fill;
     }
     for(int x=side?r->offset+r->native_width:0;x<(side?r->total:r->offset);x++){
      uint32_t&p=road_pixels[r->line*r->total+x];if((p&0xffffff)==0)p=fill;
     }
    }
    // 0.21ag: the 1-row black base line under the facade (any length, any
    // column): black with wall grey above and no black below in B.
    {
     unsigned hb=(v->reg[13]&63)<<10;int hs1=(int16_t)word(v,hb+(r->line+1)*4+2);
     for(int x=0;x<r->total;x++){
      uint32_t&p=road_pixels[r->line*r->total+x];
      if((p&0xffffff)!=0||!gp2_facade(road_pixels[(r->line-1)*r->total+x]))continue;
      if(plane_b(v,std::clamp(x-r->offset,0,r->native_width-1)-hs1,r->line+1+v->vsram[1])==0xff000000u)continue;
      p=0xffcececeu;
     }
    }
   }
   // GP II entrance facade continued by the host (see gp2_facade_band): grey
   // wall rows with a continuous top edge, and its black base row, faded in
   // during the approach. Native wall/mouth pixels (grey or black) are kept.
   if(game_index&&r->line>=65){
    static unsigned band_tick=~0u;static FacadeBand band;
    if(band_tick!=tick){band_tick=tick;band=gp2_facade_band();}
    if(band.on&&r->line>=int(std::floor(band.top))&&r->line<=band.bottom+1){
     bool base=r->line==band.bottom+1;
     double cover=(base?1.0:std::clamp(r->line+1-band.top,0.0,1.0))*band.alpha;
     uint32_t paint=base?0xff000000u:0xffcececeu;
     if(base)cover=0; // 0.21ag: no host black base line (owner: thin black lines)
     for(int xo=0;xo<r->total&&cover>0;xo++){
      uint32_t&p=road_pixels[r->line*r->total+xo];
      if((p&0xffffff)==0||(p&0xffffff)==0xcecece)continue;
      p=mix_colour(p,paint,cover);
     }
    }
   }
  }
 }
  if(tunnel_phase()>=2&&tunnel_phase()<=3&&!margin_open&&!exit_lintel_sky[r->line]&&r->line>=65&&r->line<200){
   auto&row=road_rows[r->line];unsigned hb=(v->reg[13]&63)<<10;int hs=(int16_t)word(v,hb+r->line*4+2);
   for(int side=0;side<2;side++){
    int edge=side?r->native_width-8:0,dark=0;
    for(int k=0;k<8;k++){
     // Architecture belongs to B. Sampling the finished native frame also
     // sampled tyres, signs and HUD, making the wall switch on/off with them.
     uint32_t pixel=plane_b(v,edge+k-hs,r->line+v->vsram[1]);unsigned red=(pixel>>16)&255,green=(pixel>>8)&255,blue=pixel&255;
     dark+=(red==0&&green==0&&blue==0)||(red==green&&green==blue&&red>=70&&red<=206);
    }
    // GP II: the exit aperture rows (horizon, road edge) are not walls at the
    // native edge, so the wall test left 1px..thick ground bands across the
    // margins. Its tunnel margins are wall for the whole 2..3 phase window,
    // except (a) rows where the authored entrance facade (flat grey) still
    // reaches this native edge, and (b) exit rows where the authored opening
    // already reaches it (gp2_exit_open). Forcing black there left black
    // blocks beside a grey facade at the entrance and kept a narrow black
    // frame around a full-width opening at the exit, then snapped open.
    if(game_index){
     if(exit_open[side])continue;
     int facade=0;
     for(int k=0;k<8;k++)facade+=gp2_facade(plane_b(v,edge+k-hs,r->line+v->vsram[1]));
     if(facade>=6){
      if(road_pixels.size()!=(size_t)r->total*224)road_pixels.assign(r->total*224,0);
      uint32_t grey=plane_b(v,(side?r->native_width-1:0)-hs,r->line+v->vsram[1]);
      if(!row.valid)for(int x=0;x<r->total;x++)road_pixels[r->line*r->total+x]=plane_b(v,std::clamp(x-r->offset,0,r->native_width-1)-hs,r->line+v->vsram[1]);
      row.valid=true;
      for(int x=side?r->offset+r->native_width:0;x<(side?r->total:r->offset);x++)road_pixels[r->line*r->total+x]=grey;
      continue;
     }
     dark=8;
    }
    if(dark<6)continue;
    if(road_pixels.size()!=(size_t)r->total*224)road_pixels.assign(r->total*224,0);
    if(!row.valid)for(int x=0;x<r->total;x++)road_pixels[r->line*r->total+x]=plane_b(v,std::clamp(x-r->offset,0,r->native_width-1)-hs,r->line+v->vsram[1]);
    row.valid=true;
    for(int x=side?r->offset+r->native_width:0;x<(side?r->total:r->offset);x++){
     road_pixels[r->line*r->total+x]=0xff000000u;if(!game_index)tunnel_occlusion[r->line*r->total+x]=1;
    }
   }
  }
  // GP II tunnel mouth (phases 1-3): the facade jambs are SAT sprites (flat
  // grey 0xCECECE) at the native instant, not B. The host jamb is drawn on the
  // delayed presentation clock and, in phase 1, only over B facade rows, so on
  // the panorama/road rows the jamb vanished, and the 16:9 margin beside it
  // stayed black with a single grey B row (the "linha cinza"). Keep the
  // cartridge's own jamb footprint (replaced sprite pixels of facade grey) in
  // the background and continue it into the margin from the native edge.
  // Phase 1 and the first ticks of phase 2 only: inside the tunnel the same grey
  // belongs to lit pillars, not to the facade.
  if(tunnel_phase()==1)facade_tick=tick;
  if(game_index&&smooth_zoom&&(tunnel_phase()==1||(tunnel_phase()==2&&tick-facade_tick<=24))&&r->line>=65&&road_rows[r->line].valid&&road_pixels.size()==(size_t)r->total*224){
   auto facade_sprite=[&](int x){unsigned owner=r->native_owner[x];if(!owner||owner>80||!selected[owner-1])return 0u;uint32_t c=colour(v,r->native_index[x]);return gp2_facade(c)?c:0u;};
   // 0.21ag: a jamb sliver of 1-2 px (cut by the native edge) was left as a
   // grey vertical line between black interior and black margin.
   for(int x=0;x<r->native_width;){
    if(!facade_sprite(x)){x++;continue;}
    int e=x;while(e<r->native_width&&facade_sprite(e))e++;
    if(e-x>=3)for(int k=x;k<e;k++)road_pixels[r->line*r->total+r->offset+k]=facade_sprite(k);
    x=e;
   }
   for(int side=0;side<2;side++){
    int hits=0;uint32_t c=0;for(int k=0;k<3;k++)if(uint32_t s=facade_sprite(side?r->native_width-1-k:k)){++hits;c=s;}
    if(hits<2)continue;
    for(int x=side?r->offset+r->native_width:0;x<(side?r->total:r->offset);x++)road_pixels[r->line*r->total+x]=c;
   }
  }
  // 0.21ag (owner: grey line at the entrance): once inside (phase 2) a row
  // that is still facade grey across the screen is the wall above the mouth
  // lingering at 20 Hz; inside the tunnel it is the black roof.
  if(game_index&&smooth_zoom&&tunnel_phase()==2&&r->line>=65&&r->line<200&&road_rows[r->line].valid&&road_pixels.size()==(size_t)r->total*224){
   int grey=0;for(int x=0;x<r->total;x++)grey+=gp2_facade(road_pixels[r->line*r->total+x]);
   if(grey*10>=r->total*9)for(int x=0;x<r->total;x++)if(gp2_facade(road_pixels[r->line*r->total+x]))road_pixels[r->line*r->total+x]=0xff000000u;
  }
  // GP II, first ticks after the tunnel (phase 0): the authored wall jamb of the
  // exit can still stand at a native edge (whole B edge black on panorama/road
  // rows). The margins had opened to scenery beside it: a cut wall. Continue the
  // wall into that margin while the native edge is wall.
  if(getenv("SMGP_GP2_EXIT_LINTEL")&&game_index&&smooth_zoom&&tunnel_phase()==0&&tick-last_tunnel_tick<=30&&r->line>=65&&r->line<200&&road_rows[r->line].valid&&road_pixels.size()==(size_t)r->total*224){
   unsigned hb=(v->reg[13]&63)<<10;int hs=(int16_t)word(v,hb+r->line*4+2);
   for(int side=0;side<2;side++){
    int wall=0;for(int k=0;k<4;k++)wall+=plane_b(v,(side?r->native_width-1-k:k)-hs,r->line+v->vsram[1])==0xff000000u;
    if(wall<4)continue;
    for(int x=side?r->offset+r->native_width:0;x<(side?r->total:r->offset);x++)road_pixels[r->line*r->total+x]=0xff000000u;
   }
  }
  // 0.21ag exit backdrop: the cartridge's own exit frame (structure sprites,
  // removed from the sprite layer) is painted into the background at its
  // native pixels, behind every host-drawn actor.
  // Phase 0: outside; the frame remnants beside the camera are not painted.
  if(smooth_zoom&&gp2_native_exit()&&tunnel_phase()!=0&&!exit_lintel_sky[r->line]&&r->line>=65&&road_rows[r->line].valid&&road_pixels.size()==(size_t)r->total*224){
   for(int x=0;x<r->native_width;x++){
    unsigned owner=r->native_owner[x];if(!owner||owner>80||!selected[owner-1])continue;
    unsigned actor=displayed_owner[owner-1];if(!actor||std::find(exit_backdrop_actors.begin(),exit_backdrop_actors.end(),actor)==exit_backdrop_actors.end())continue;
    road_pixels[r->line*r->total+r->offset+x]=colour(v,r->native_index[x]);
   }
  }

 for(int x=0;x<r->total;x++){
  // Guest foreground priority is qualified only inside H32. Outside it,
  // hidden B-plane cells are storage: their priority bits must not cover
  // reconstructed road pixels or erase imported/scaled world sprites.
  if(x<r->offset||x>=r->offset+r->native_width){foreground[r->line*r->total+x]=0;continue;}
  unsigned owner=r->owner[x];
  bool keep=owner>0&&(owner>80||!selected[owner-1]);
  unsigned overlay=r->foreground[x];
  // Inside the tunnel the ceiling/wall is behind on-road vehicles, while
  // roadside objects remain behind its footprint. The native VDP pass and
  // status are unchanged; the enlarged car must not be erased by a roof
  // blanking band made for the much smaller cartridge mip.
  if(tunnel_phase()>=1&&tunnel_phase()<=3&&r->line>=65&&r->line<200&&overlay<64&&foreground_pattern(v,x-r->offset,r->line)==~0u){
   tunnel_occlusion[r->line*r->total+x]=1;overlay=255;
  }
  // 0.21ag: the same roof blanking tiles linger a few frames after the exit
  // (a black band across the top). Outside the tunnel they are dropped.
  else if(smooth_zoom&&tunnel_phase()==0&&last_tunnel_tick&&tick-last_tunnel_tick<=60&&r->line>=65&&r->line<200&&overlay<64&&foreground_pattern(v,x-r->offset,r->line)==~0u)overlay=255;
  if(!keep&&player_arcade()&&r->line>=192){
   // The normal player body/tyres live in WINDOW, not the SAT. Only opaque
   // texels of its qualified 37-pattern allocation are replaced. HUD tiles,
   // transparent holes, tachometer and speed glyphs retain their provenance.
   unsigned tile=foreground_pattern(v,x-r->offset,r->line);
   if(player_tile(tile)){
    if(player_underlay.size()!=(size_t)r->total*224)player_underlay.assign(r->total*224,0);
    unsigned hb=(v->reg[13]&63)<<10;int hs=(int16_t)word(v,hb+r->line*4+2);
    player_underlay[r->line*r->total+x]=plane_b(v,x-r->offset-hs,r->line+v->vsram[1]);
    foreground[r->line*r->total+x]=0;++player_window_pixels;continue;
   }
  }
  uint32_t pixel=keep?colour(v,r->index[x]):(overlay<64?colour(v,overlay):0);
  if(keep&&smooth_zoom&&championship()&&r->line<65&&owner<=80){
   int team=team_for_actor(displayed_mirror_owner[owner-1]);unsigned index=r->index[x],nibble=index&15;
   if(team>=0&&nibble>=6&&nibble<=9){
    unsigned white=genesis_dac_cram_to_argb(v->cram[(index&48)+3],GENESIS_DAC_NORMAL)&255;
    int level=index>=128?GENESIS_DAC_HIGHLIGHT:(index>=64?GENESIS_DAC_SHADOW:GENESIS_DAC_NORMAL);
    auto paint=team_paint(team,white,level);int role=nibble==6?2:(nibble==7?3:(nibble==8?0:1));
    pixel=paint[role];++mirror_team_pixels;
   }
  }
  foreground[r->line*r->total+x]=pixel;
 }
}
int skyline(void*,const GVDP*v,int line,int x,int index){
 return index;
}
void clip_fixed(void*,const GVDP*v,const GVDPSpriteLayer*layer){
 const int w=gvdp_screen_width(v);
 for(int x=0;x<layer->total;x++){
  if(x>=layer->offset&&x<layer->offset+w)continue;
  unsigned owner=layer->owner?layer->owner[x]:0;
  if(!owner)continue;
  // Every guest SAT sprite retains its native viewport. Only the qualified
  // forward-world pass may extend it; mirror/HUD identity never uses a tile
  // threshold shared with the front view.
  layer->opaque[x]=0;
 }
}
void release(){for(auto **p:{&target,&background,&sprite,&front,&road,&scenery_target,&scenery_mask}){if(*p)SDL_DestroyTexture(*p);*p=nullptr;}texture_width=texture_height=0;}
const uint32_t *recover_native(const uint32_t*input,int stride,int width,int height){
 composed.assign(input,input+(size_t)stride*height);
 if(native_frame.size()==256*224){
  for(int y=0;y<224;y++)std::copy_n(native_frame.data()+y*256,256,composed.data()+y*stride+offset_width);
  renderer_ready=false;fprintf(stderr,"[SMGP GPU] composition failed; native sprite fallback enabled\n");return composed.data();
 }
 for(auto it=pieces.rbegin();it!=pieces.rend();++it){const auto&p=*it;
  for(int y=0;y<p.h;y++)for(int x=0;x<p.w;x++){
   int dx=offset_width+p.x+x,dy=p.y+y;uint32_t pixel=p.pixels[y*p.w+x];
   if(pixel&&dx>=0&&dx<width&&dy>=65&&dy<height)composed[dy*stride+dx]=pixel;
  }
 }
 if(foreground.size()==(size_t)width*height)for(int y=0;y<height;y++)for(int x=0;x<width;x++)
  if(uint32_t pixel=foreground[y*width+x])composed[y*stride+x]=pixel;
 renderer_ready=false;
 fprintf(stderr,"[SMGP GPU] composition failed; native sprite fallback enabled\n");
 return composed.data();
}
}

extern "C" void smgp_scene_begin(int game,uint64_t frame,int racing,int enabled){
 if(frame<tick){result_retire=false;last_race_background.clear();last_race_width=0;}
 const char*aspect=getenv("SMGP_ASPECT");bool wide=!aspect||std::string(aspect)!="4:3";
 smooth_zoom=enabled;
 original_world_pixels=leaked_world_pixels=unsupported_slots=0;
 player_window_pixels=mirror_team_pixels=0;
 std::fill(player_underlay.begin(),player_underlay.end(),0);
 std::fill(tunnel_occlusion.begin(),tunnel_occlusion.end(),0);
 std::fill(portal_surface.begin(),portal_surface.end(),0);
 tick=(unsigned)frame;game_index=game;active=racing&&(enabled||wide)&&renderer_ready;prepared=false;compose_key=~0ull;
 formation_active=enabled&&formation_page();formation_line=-1;
 if(ending_active&&!rw(profile().pause))++ending_age;
 if(racing){
  double pano=native_panorama();
  if(!panorama_valid){observed_panorama=pano;panorama_stamp=tick;observed_panorama_speed=0;panorama_valid=true;}
  else if(pano!=observed_panorama){
   double delta=panorama_difference(pano,observed_panorama);unsigned dt=tick-panorama_stamp;
   observed_panorama_speed=dt&&dt<=6&&std::abs(delta)<48?std::clamp(delta/dt,-8.0,8.0):0;
   if(std::abs(delta)>=48)render_clock.valid=false;
   observed_panorama=pano;panorama_stamp=tick;
  }
  double lateral=(int16_t)rw(game_index?0xfc44:0x9204);
  if(!lateral_valid){observed_lateral=lateral;lateral_from=lateral;lateral_stamp=tick;lateral_velocity=0;lateral_valid=true;lateral_clock.clear();lateral_clock.push(tick,lateral);}
  else if(lateral!=observed_lateral){unsigned dt=tick-lateral_stamp;lateral_velocity=dt&&dt<=6?std::clamp((lateral-observed_lateral)/dt,-16.0,16.0):0;
   // Each commit starts a new segment from the value the OLD segment gives at this
   // very tick (its target when the interval has elapsed). Starting from the value
   // shown on the previous tick left the last third of every segment unplayed and
   // stalled the camera for one tick per commit (-8,-8,0,-8,-8,0 measured), a 20 Hz
   // shake of the whole scene. Never passes its target.
   double old_span=std::max(1.0,(double)lateral_interval);
   double old_value=lateral_from+(observed_lateral-lateral_from)*std::min(1.0,(double)dt/old_span);
   if(dt>=1&&dt<=6)lateral_interval=dt;
   // On a native direction reversal the old target lies on the far side of the new
   // one: keep the start between what is shown and the new target, so the camera
   // never moves away from, or beyond, the confirmed value.
   if(render_clock.valid)old_value=std::clamp(old_value,std::min(render_clock.lateral,lateral),std::max(render_clock.lateral,lateral));
   lateral_from=render_clock.valid?old_value:observed_lateral;observed_lateral=lateral;lateral_stamp=tick;lateral_clock.push(tick,lateral);}
  double lap_position=rl(profile().player+26)/65536.0;
  // +30 is the road builder's absolute longitudinal coordinate. On GP II
  // it can commit one VBlank before the lap-relative +26 copy. Observing
  // only that later copy forecast past the already confirmed position and
  // then corrected backwards. Fold the earliest authoritative coordinate
  // into the same lap epoch; never mix the two commit times as velocities.
  double current=lap_position+phase_difference(rl(profile().player+30)/65536.0,lap_position);
  if(!observed_valid){observed_phase=current;observed_stamp=tick;observed_velocity=0;observed_valid=true;phase_samples.clear();push_phase_sample(tick,current);}
  else if(current!=observed_phase){double delta=phase_difference(current,observed_phase);unsigned dt=tick-observed_stamp;
   if(std::abs(delta)>12){render_clock.valid=false;phase_samples.clear();if(getenv("SMGP_MOTION_TRACE"))fprintf(stderr,"[SMGP CLOCK RESET] frame=%u delta=%.6f reason=native_rebase\n",tick,delta);}
   observed_velocity=dt&&dt<=6&&std::abs(delta)<12?std::clamp(delta/dt,0.0,3.0):0;
   if(getenv("SMGP_PHASE_TRACE"))fprintf(stderr,"[SMGP PHASE SAMPLE] frame=%u native=%.6f delta=%.6f dt=%u\n",tick,current,delta,dt);
   observed_phase=current;observed_stamp=tick;push_phase_sample(tick,current);
  }
  if(!render_clock.valid||render_clock.length!=rw(profile().length)){render_clock={};render_clock.phase=current;render_clock.lateral=lateral;render_clock.panorama=pano;render_clock.length=rw(profile().length);render_clock.valid=true;}
  else if(!rw(profile().pause)){
   // Forecast from fractional observations, then absorb the new sample's
   // residual gradually. Re-anchoring a separate integer predictor at each
   // SAT upload made the background step forward and back at builder cadence.
   double pv=tick-panorama_stamp<=6?observed_panorama_speed:0;
   double pt=observed_panorama+pv*std::min(3u,tick-panorama_stamp);
   render_clock.panorama_speed+=(pv-render_clock.panorama_speed)*.5;
   double pc=panorama_difference(pt,render_clock.panorama+render_clock.panorama_speed)*.35;
   double ps=render_clock.panorama_speed+std::clamp(pc,-2.0,2.0);
   // Deceleration can put the prediction slightly ahead of the next sample.
   // Consume that residual by slowing/holding, never by moving backwards
   // against the observed direction. A real native reversal changes pv.
   double bound=std::max(.05,std::abs(pv)*1.75);
   render_clock.panorama+=pv>0?std::clamp(ps,0.0,bound):pv<0?std::clamp(ps,-bound,0.0):0;
   if(camera_interrupted()){
    // An interruption (impact/stop) must not move the presented camera
    // backwards: the observed value lags the presented clock by a commit, and
    // snapping to it reversed the road and every roadside object for a tick
    // (measured 2357 -> 2344). Only a real native rebase (|delta| > 12, handled
    // above) may reset the clock; otherwise hold, never reverse.
    double held=0;
    if(phase_fit(tick,held,false)){
     double ph=observed_phase+phase_difference(held,phase_samples.back().unwrapped);
     double length=rw(profile().length);if(length>100)ph=std::fmod(ph+length,length);
     // Delayed interpolation never reaches beyond the last confirmed value and
     // never reverses; it simply finishes the committed motion and stops.
     if(phase_difference(ph,render_clock.phase)>=0)render_clock.phase=ph;
    }else{
     double back=phase_difference(current,render_clock.phase);
     if(back>=0)render_clock.phase=current;
    }
    render_clock.velocity=0;
    double lat_held=0;
    if(lateral_clock.smooth(tick,presentation_delay(),lateral_clock.period,lat_held)){
     // An impact kicks the native lateral by tens of units per commit; follow it
     // on the same delayed timeline instead of jumping the whole scene.
     double pos=render_clock.lateral,want=observed_lateral-pos,step=lat_held-pos;
     if(step*want<0)step=0;else if(std::abs(step)>std::abs(want))step=want;
     render_clock.lateral_velocity=step;render_clock.lateral=pos+step;
    }else{render_clock.lateral=lateral;render_clock.lateral_velocity=0;}
   }
   else{
    double predicted=0;
    if(phase_fit(tick,predicted,false)){
     // Smooth presentation: the fitted committed line, forecast <= 3 ticks.
     double ph=observed_phase+phase_difference(predicted,phase_samples.back().unwrapped);
     double length=rw(profile().length);if(length>100)ph=std::fmod(ph+length,length);
     render_clock.phase=ph;render_clock.velocity+=(observed_velocity-render_clock.velocity)*.5;
    }else{
    double velocity=tick-observed_stamp<=6?observed_velocity:0;
    double target=observed_phase+velocity*std::min(3u,tick-observed_stamp);
    render_clock.velocity+=(velocity-render_clock.velocity)*.5;
    // Each new sample pulled the clock back by a fraction of a tick. With the
    // step floored at zero this held the presented camera for 1-2 VBlanks and
    // then advanced in a burst every 3 ticks: roadside objects stepped sideways
    // and in depth with that cadence. Limit the residual correction to half the
    // observed speed, so the clock keeps moving forward smoothly at the measured
    // rate and only converges, never stops or reverses.
    double correction=phase_difference(target,render_clock.phase+render_clock.velocity)*.35;
    double half=std::abs(velocity)*.5;correction=std::clamp(correction,-half,half);
    double step=std::clamp(render_clock.velocity+correction,0.0,std::max(.05,velocity*1.75));
    render_clock.phase+=step;double length=rw(profile().length);if(length>100){render_clock.phase=std::fmod(render_clock.phase+length,length);}
    }
    double lv=tick-lateral_stamp<=6?lateral_velocity:0;
    double lat_delayed=0;
    if(lateral_clock.smooth(tick,presentation_delay(),lateral_clock.period,lat_delayed)){
     // Follows the confirmed FC44/9204 samples one logic step behind, but never
     // moves away from the latest confirmed value nor beyond it (a native
     // reversal makes the delayed path cross it).
     double pos=render_clock.lateral,want=observed_lateral-pos,step=lat_delayed-pos;
     if(step*want<0)step=0;else if(std::abs(step)>std::abs(want))step=want;
     render_clock.lateral_velocity=step;render_clock.lateral=pos+step;
    }else if(game_index){
     // FC44 is committed in native batches (about every 3 VBlanks). Chasing
     // each confirmed value with a fixed gain made the whole scene step
     // sideways at that cadence (near props then reversed on the step ticks),
     // and forecasting past it recoiled. Never go beyond the confirmed value:
     // close the remaining error LINEARLY over the measured commit interval,
     // so the camera arrives as the next commit is due, at constant speed.
     // Constant speed from the on-screen value to the confirmed target over one
     // commit interval, then hold. Evaluated from the commit stamp, not
     // accumulated, so a late commit cannot snap the camera in one VBlank.
     double span=std::max(1.0,(double)lateral_interval);
     double fraction=std::min(1.0,(double)(tick-lateral_stamp)/span);
     render_clock.lateral=lateral_from+(observed_lateral-lateral_from)*fraction;
     render_clock.lateral_velocity=tick-lateral_stamp<span?(observed_lateral-lateral_from)/span:0;
    }else{
    double lt=observed_lateral+lv*std::min(3u,tick-lateral_stamp);
    if(lv*render_clock.lateral_velocity<0)render_clock.lateral_velocity=lv;
    else render_clock.lateral_velocity+=(lv-render_clock.lateral_velocity)*.5;
    double lateral_step=render_clock.lateral_velocity+std::clamp((lt-render_clock.lateral-render_clock.lateral_velocity)*.35,-4.0,4.0);
    // Never turn a new positive/negative physical sample into an opposite
    // correction on its following VBlank. Near sprites magnify that small
    // lateral ringing into visible high-speed shake. Real native direction
    // changes are honoured immediately. At rest settle without overshooting.
    double lateral_bound=std::max(.05,std::abs(lv)*1.75);
    if(lv>0)lateral_step=std::clamp(lateral_step,0.0,lateral_bound);
    else if(lv<0)lateral_step=std::clamp(lateral_step,-lateral_bound,0.0);
    else lateral_step=std::clamp((observed_lateral-render_clock.lateral)*.35,-4.0,4.0);
    render_clock.lateral+=lateral_step;
    }
   }
  }
  if(getenv("SMGP_MOTION_TRACE"))fprintf(stderr,"[SMGP PANORAMA] frame=%u native=%.6f presented=%.6f velocity=%.6f sample=%u paused=%d\n",tick,pano,present_panorama(),observed_panorama_speed,panorama_stamp,rw(profile().pause)!=0);
  if(FILE*tf=telemetry_file())fprintf(tf,"%u,%.3f,%.4f,%.5f,%.5f,%u,%d,%u,%d\n",tick,lateral,render_clock.valid?render_clock.lateral:lateral,current,render_clock.valid?render_clock.phase:current,game_index?rw(0xfc2a):rw(0x9108),game_index?(int)(int16_t)rw(0xfc2c):0,game_index?rw(0xfc20):0,camera_interrupted()?1:0);
  if(getenv("SMGP_PHASE_TRACE"))fprintf(stderr,"[SMGP PHASE PRESENT] frame=%u presented=%.6f observed=%.6f\n",tick,render_clock.valid?render_clock.phase:current,observed_phase);
  if(getenv("SMGP_MOTION_TRACE"))fprintf(stderr,"[SMGP CAMERA LATERAL] frame=%u native=%.6f presented=%.6f velocity=%.6f sample=%u paused=%d interrupted=%d\n",tick,lateral,render_clock.lateral,lateral_velocity,lateral_stamp,rw(profile().pause)!=0,camera_interrupted());
  if(getenv("SMGP_PLAYER_TRACE"))fprintf(stderr,"[SMGP PLAYER STATE] frame=%u map=%X routine=%X pause=%u mode=%u\n",tick,rl(profile().player+4),rl(profile().player),rw(profile().pause),game_index?0:rw(0x9140));
 }
 if((active||result_retire)&&smooth_zoom)load_arcade();
 if(!racing){
  chassis=ChassisState{};
  if(!result_retire){ending_active=false;ending_age=0;ending_banner={};ending_background.clear();}
  observed_valid=false;panorama_valid=false;render_clock={};
  lateral_valid=false;precise_center=false;lateral_clock.clear();
  pending_objects.clear();world_objects.clear();objects_ready=false;
  build_serial=committed_serial=world_serial=assembled_serial=0;
  scene_queue.clear();motion_queue.clear();removal_objects.clear();building_owner.fill(0);displayed_owner.fill(0);
  building_mirror_owner.fill(0);displayed_mirror_owner.fill(0);
  prop_source_tiles.clear();
  effect_clocks.clear();
  planted_trees.clear();tree_track_length=0;
 }
 if(!active){history.clear();pieces.clear();groups.clear();road_rows={};road_pixels.clear();}
 gvdp_set_visual_sprites(active?transform:nullptr,nullptr);
 gvdp_set_visual_audit(racing||formation_active?audit:nullptr,nullptr);
 gvdp_set_visual_objects(racing?clip_fixed:nullptr,nullptr);
 gvdp_set_visual_surface(racing?skyline:nullptr,nullptr);
}
extern "C" void smgp_scene_renderer(SDL_Renderer *r){
 SDL_RendererInfo info{};
 renderer_ready=r&&SDL_GetRendererInfo(r,&info)==0&&(info.flags&SDL_RENDERER_ACCELERATED)&&(info.flags&SDL_RENDERER_TARGETTEXTURE);
 if(!renderer_ready)fprintf(stderr,"[SMGP GPU] hardware composition unavailable; preserving native sprites\n");
}
static const uint32_t *gpu_compose_inner(SDL_Renderer*r,const uint32_t*input,int stride,int width,int height,uint64_t frame);
// The achievement toast and F6 panel are host overlays: draw them after the GPU composition,
// otherwise the road, HUD and sprites composed here would cover them during races.
extern "C" const uint32_t *smgp_gpu_compose(SDL_Renderer*r,const uint32_t*input,int stride,int width,int height,uint64_t frame){
 const uint32_t*picture=gpu_compose_inner(r,input,stride,width,height,frame);
 if(!picture||!smgp_ach_visible()||height!=224)return picture;
 static std::vector<uint32_t> with_overlay;
 with_overlay.assign(picture,picture+size_t(stride)*height);
 for(int line=0;line<height;line++)smgp_ach_overlay(line,with_overlay.data()+size_t(line)*stride,width);
 return with_overlay.data();
}
static const uint32_t *gpu_compose_inner(SDL_Renderer*r,const uint32_t*input,int stride,int width,int height,uint64_t frame){
 if(result_retire&&smooth_zoom&&height==224&&r){
  load_arcade();
  if(shared_retire.width){
   if(!ending_active){ending_banner=shared_retire;ending_is_retire=true;ending_age=0;ending_active=true;ending_anchor_x=(256-shared_retire.width)/2;ending_anchor_y=128-shared_retire.height/2;}
   if(ending_background.size()!=size_t(stride)*height){ending_background.assign(input,input+size_t(stride)*height);ending_width=width;}
   composed.assign(size_t(stride)*height,0xff000000u);
   // The result changes H32 to H40. Preserve the frozen picture's DISPLAY
   // aspect while the presentation canvas changes 342->426 (etc.); simply
   // centering old raw pixels squeezed the road horizontally at that switch.
   for(int y=0;y<height;y++)for(int x=0;x<width;x++){
    int sx=std::clamp(int((x+.5)*ending_width/width),0,std::max(0,ending_width-1));
    composed[y*stride+x]=ending_background[y*stride+sx];
   }
   ending_overlay(composed,stride,width,height);return composed.data();
  }
 }
 if(!active||!prepared||!r||height!=224)return input;
 if(ending_active&&!ending_background.empty()&&ending_width==width&&ending_background.size()==size_t(stride)*height){composed=ending_background;ending_overlay(composed,stride,width,height);return composed.data();}
 load_arcade();
 if(compose_key==frame&&composed.size()==(size_t)stride*height)return composed.data();
 if(width!=texture_width||height!=texture_height){
  release();target=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,width*coverage_scale,height*coverage_scale);
  background=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,width,height);
  sprite=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,512,256);
  front=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,width,height);
  road=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,width,height);
  scenery_target=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,width*coverage_scale,height*coverage_scale);
  scenery_mask=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,width,height);
  SDL_BlendMode matte=SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO,SDL_BLENDFACTOR_SRC_ALPHA,SDL_BLENDOPERATION_ADD,SDL_BLENDFACTOR_ZERO,SDL_BLENDFACTOR_SRC_ALPHA,SDL_BLENDOPERATION_ADD);
  if(!target||!background||!sprite||!front||!road||!scenery_target||!scenery_mask||SDL_SetTextureBlendMode(scenery_mask,matte)!=0){release();return recover_native(input,stride,width,height);}
  SDL_SetTextureBlendMode(scenery_target,SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(scenery_target,SDL_ScaleModeNearest);SDL_SetTextureScaleMode(scenery_mask,SDL_ScaleModeNearest);
  SDL_SetTextureBlendMode(sprite,SDL_BLENDMODE_BLEND);SDL_SetTextureBlendMode(front,SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(sprite,SDL_ScaleModeNearest);
  SDL_SetTextureScaleMode(background,SDL_ScaleModeNearest);SDL_SetTextureScaleMode(front,SDL_ScaleModeNearest);SDL_SetTextureScaleMode(road,SDL_ScaleModeNearest);
  texture_width=width;texture_height=height;
 }
 SDL_Texture *old=SDL_GetRenderTarget(r);int lw,lh;SDL_RenderGetLogicalSize(r,&lw,&lh);
 SDL_Rect viewport,clip;SDL_RenderGetViewport(r,&viewport);SDL_RenderGetClipRect(r,&clip);
 SDL_bool clipped=SDL_RenderIsClipEnabled(r);float sx,sy;SDL_RenderGetScale(r,&sx,&sy);
 SDL_SetRenderTarget(r,target);SDL_RenderSetLogicalSize(r,0,0);SDL_RenderSetScale(r,coverage_scale,coverage_scale);
 SDL_RenderSetViewport(r,nullptr);SDL_RenderSetClipRect(r,nullptr);
 SDL_Rect full{0,0,width,height};
 crisp_interior.assign(size_t(width)*height,0);
 const uint32_t*background_pixels=input;
 if(player_window_pixels&&player_underlay.size()==(size_t)width*height){
  composed.resize((size_t)stride*height);std::copy_n(input,(size_t)stride*height,composed.data());
  for(int y=0;y<height;y++)for(int x=0;x<width;x++)if(auto pixel=player_underlay[y*width+x])composed[y*stride+x]=pixel;
  background_pixels=composed.data();
 }
 // Owner 0.21ac (World entrance, white line blinking): inside the tunnel
 // window the margins continue the native edge of each row. Where a thin
 // diagonal (the kerb highlight) meets the edge, one or two rows carried it
 // across the whole margin as a full-width line that flickered as it moved.
 // In the margins only, a feature 1-2 rows thin between equal neighbours
 // takes that neighbour's colour. Native columns are untouched.
 if(smooth_zoom&&tunnel_phase()>=1&&tunnel_phase()<=3&&offset_width>0&&width>offset_width*2){
  const bool has_road=road_pixels.size()==(size_t)width*height;
  auto under=[&](int y,int x)->uint32_t{return has_road&&y>=65&&road_rows[y].valid?road_pixels[size_t(y)*width+x]:background_pixels[size_t(y)*stride+x];};
  std::vector<std::pair<size_t,uint32_t>> fixes;
  for(int x=0;x<width;x++){
   if(x>=offset_width&&x<width-offset_width)continue;
   for(int y=66;y<height-2;y++){
    uint32_t up=under(y-1,x),c=under(y,x),dn=under(y+1,x),dn2=under(y+2,x);
    if(c==up)continue;
    uint32_t to=0;bool fix=false;
    if(c!=dn&&up==dn){to=up;fix=true;}
    else if(c==dn&&c!=dn2&&up==dn2){to=up;fix=true;}
    if(fix){fixes.push_back({size_t(y)*width+x,to});if(c==dn)fixes.push_back({size_t(y+1)*width+x,to});}
   }
  }
  // Owner 0.21ad (GP II/World, inside the tunnel): a one-row horizontal stub
  // of road-edge material (>= 10 px, black wall right above and below it)
  // blinked beside the exit and at the native left edge. A real feature of
  // the floor is diagonal (short per-row runs) or touches lit rows; a long
  // flat 1-row stub floating in the wall is not. Paint it as the wall.
  if(tunnel_phase()>=2)for(int y=66;y<height-1;y++){
   auto dark=[&](uint32_t p){return std::max({(p>>16)&255u,(p>>8)&255u,p&255u})<24;};
   int run=0;
   for(int x=0;x<=width;x++){
    bool stub=false;
    if(x<width){uint32_t c=under(y,x);stub=!dark(c)&&dark(under(y-1,x))&&dark(under(y+1,x));}
    if(stub){++run;continue;}
    if(run>=10)for(int k=x-run;k<x;k++)fixes.push_back({size_t(y)*width+k,0xff000000u});
    run=0;
   }
  }
  if(!fixes.empty()){
   if(background_pixels!=composed.data()){composed.resize((size_t)stride*height);std::copy_n(input,(size_t)stride*height,composed.data());background_pixels=composed.data();}
   for(auto [n,to]:fixes){
    int y=int(n/width),x=int(n%width);
    if(has_road&&road_rows[y].valid)road_pixels[n]=to;else composed[size_t(y)*stride+x]=to;
   }
  }
 }
 SDL_UpdateTexture(background,nullptr,background_pixels,stride*4);SDL_RenderCopy(r,background,nullptr,&full);
 SDL_Rect road_clip{0,65,width,height-65};SDL_RenderSetClipRect(r,&road_clip);
 bool mask_scenery=tunnel_occlusion.size()==(size_t)width*height&&std::any_of(tunnel_occlusion.begin(),tunnel_occlusion.end(),[](uint8_t p){return p!=0;});
 if(mask_scenery){
  scenery_alpha.resize((size_t)width*height);
  for(size_t n=0;n<scenery_alpha.size();n++)scenery_alpha[n]=tunnel_occlusion[n]?0x00ffffffu:0xffffffffu;
  SDL_UpdateTexture(scenery_mask,nullptr,scenery_alpha.data(),width*4);
 }
 if(road_pixels.size()==(size_t)width*height){
  extend_far_road(width,offset_width);
  SDL_UpdateTexture(road,nullptr,road_pixels.data(),width*4);
  for(int y=65;y<224;y++)if(road_rows[y].valid){
   SDL_Rect line{0,y,width,1};SDL_RenderCopy(r,road,&line,&line);
  }
 }
 // Assemble the largest authored pose BEFORE scaling. Internal tile edges
 // share one transform and one GPU quad, including pieces outside native H32.
 std::vector<int> order;for(int gi=int(groups.size())-1;gi>=0;gi--)order.push_back(gi);
 std::stable_sort(order.begin(),order.end(),[](int a,int b){return groups[a].z>groups[b].z;});
 for(int gi:order){
  const auto&g=groups[gi];if(g.culled||!g.full||g.w<=0||g.h<=0||g.w>512||g.h>256)continue;
  if(g.angle>=0&&getenv("SMGP_QA_INPUT_ONLY"))if(const char*only=getenv("SMGP_QA_ONLY_CAR"))if(g.id!=int(strtoul(only,nullptr,16)))continue;
  if(g.angle>=0&&getenv("SMGP_QA_INPUT_ONLY")&&getenv("SMGP_QA_HIDE_CARS"))continue;
  int aw=(int)g.w,ah=(int)g.h;std::vector<uint32_t> asset((size_t)aw*ah,0);
  for(auto it=pieces.rbegin();it!=pieces.rend();++it){const auto&p=*it;if(p.group!=gi)continue;
   for(int y=0;y<p.h;y++)for(int x=0;x<p.w;x++)if(auto pixel=p.pixels[y*p.w+x])asset[((int)(p.y-g.y)+y)*aw+(int)(p.x-g.x)+x]=pixel;
  }
  if(const char*directory=getenv("SMGP_SOURCE_EXPORT_DIR")){
   static std::set<unsigned> exported;
   if(exported.insert(g.signature).second){char name[64];snprintf(name,sizeof(name),"/map-%X.png",g.signature);png_write_argb((std::string(directory)+name).c_str(),asset.data(),aw,ah,aw);}
  }
  SDL_FRect dst{(float)(offset_width+g.from_x+g.x*g.scale),(float)(g.from_foot+g.y*g.scale),(float)(g.w*g.scale),(float)(g.h*g.scale)};
  auto vehicle_rect=[&](double cx,double foot,double w,double h){
   double grown=w*g.zoom;
   // Keep the inward flank on its physical ray while the close car grows
   // outward. The finite alongside sweep clears the viewport before culling.
   double side=std::clamp((cx-offset_width-128)/std::max(1.0,w*.5),-1.0,1.0);
   cx+=side*(grown-w)*.5;
   if(g.z<0){double t=std::clamp(-g.z/5.0,0.0,1.0);cx+=g.passing_side*(width+grown)*t*t;}
   return SDL_FRect{float(cx-grown*.5),float(foot-h*g.zoom),float(grown),float(h*g.zoom)};
  };
  if(smooth_zoom&&g.angle>=0)dst=vehicle_rect(offset_width+g.from_x+(g.x+g.w*.5)*g.scale,g.from_foot+(g.y+g.h)*g.scale,g.w*g.scale,g.h*g.scale);
  else if(g.architecture){
   // Grow upward/outward from the original ground and inner jamb anchor.
   // The road opening stays aligned; the complete maximum pose is one quad.
   dst={float(offset_width+g.from_x+g.x*g.scale*g.zoom),float(g.from_foot+g.y*g.scale*g.zoom),float(g.w*g.scale*g.zoom),float(g.h*g.scale*g.zoom)};
  }
  bool imported=false;
  if(g.flagman){asset=flagman_art.pixels;aw=flagman_art.width;ah=flagman_art.height;unsigned white=scene_fade(&g_machine.vdp);if(white<255)for(auto&p:asset)if(p>>24)p=paint_mix(0xff000000u,p,white);imported=true;}
  if(g.tree>=0){const auto&a=arcade_trees[g.tree];asset=a.pixels;aw=a.width;ah=a.height;unsigned white=scene_fade(&g_machine.vdp);if(white<255)for(auto&p:asset)if(p>>24)p=paint_mix(0xff000000u,p,white);imported=true;}
  if(smooth_zoom&&game_index&&g.angle>=0){
   export_gp2_source(gi,g,aw,ah);
   // GP II retains its complete native source, colours and silhouette. Only
   // neutral tread texels receive the same authored phase ordering as World.
   ArcadeArt native;native.width=aw;native.height=ah;native.pixels=asset;tire_frames(native);
   asset=native.rolling[g.wheel&3];
   if(g.team>=0){
    auto paint=team_paint(g.team,scene_fade(&g_machine.vdp));
    for(auto it=pieces.rbegin();it!=pieces.rend();++it){const auto&p=*it;if(p.group!=gi)continue;
     for(int y=0;y<p.h;y++)for(int x=0;x<p.w;x++){
      int sx=(p.flip&1)?p.w-1-x:x,sy=(p.flip&2)?p.h-1-y:y;unsigned tile=(p.tile+(sx/8)*(p.h/8)+sy/8)&2047;
      unsigned byte=g_machine.vdp.vram[(tile*32+(sy&7)*4+(sx&7)/2)&65535],nib=(sx&1)?byte&15:byte>>4;
      int role=nib==8?0:nib==9?1:nib==6?2:nib==7?3:-1;
      if(role>=0)asset[(int(p.y-g.y)+y)*aw+int(p.x-g.x)+x]=paint[role];
     }
    }
    if(scene_fade(&g_machine.vdp)==255)if(const char*dir=getenv("SMGP_PAINT_EXPORT_DIR")){
     static std::set<std::string>saved;std::string name="gp2-team"+std::to_string(g.team)+"-angle"+std::to_string(g.angle);
     if(saved.insert(name).second)save_rgba(std::filesystem::path(dir)/(name+".rgba"),asset,aw,ah);
    }
    if(getenv("SMGP_TEAM_TRACE"))fprintf(stderr,"[SMGP GP2 TEAM] frame=%u object=%04X team=%d paint=%08X,%08X,%08X,%08X\n",tick,g.id,g.team,paint[0],paint[1],paint[2],paint[3]);
   }
   if(const char*dir=getenv("SMGP_NATIVE_WHEEL_EXPORT_DIR"))if(scene_fade(&g_machine.vdp)==255){
    static std::set<unsigned> saved;
    if(saved.insert(g.signature).second)for(int phase=0;phase<4;phase++)
     save_rgba(std::filesystem::path(dir)/("map"+std::to_string(g.signature)+"-phase"+std::to_string(phase)+".rgba"),native.rolling[phase],aw,ah);
   }
  }
  bool gp2_import=smooth_zoom&&!game_index&&g.angle>=0&&!gp2_cars[g.angle].empty();
  if(gp2_import){
   const auto&s=gp2_cars[g.angle][g.wheel%gp2_cars[g.angle].size()];aw=s.art.width;ah=s.art.height;asset=s.art.pixels;
   unsigned white=scene_fade(&g_machine.vdp);const unsigned roles[]={8,9,6,7};std::array<uint32_t,4>paint{};
   for(int i=0;i<4;i++)paint[i]=genesis_dac_cram_to_argb(g_machine.vdp.cram[g.palette*16+roles[i]],GENESIS_DAC_NORMAL);
   if(g.team>=0)paint=team_paint(g.team,white);
   for(size_t i=0;i<asset.size();i++)if(asset[i]>>24){unsigned nib=s.ink[i]&15;int role=nib==8?0:nib==9?1:nib==6?2:nib==7?3:-1;asset[i]=role>=0?paint[role]:nib==10?genesis_dac_cram_to_argb(g_machine.vdp.cram[(g.palette*16+10)&63],GENESIS_DAC_NORMAL):paint_mix(0xff000000u,asset[i],white);}
   dst=vehicle_rect(offset_width+g.from_x+(s.x+aw*.5)*g.scale,g.from_foot+(s.y+ah)*g.scale,aw*g.scale,ah*g.scale);
   imported=true;
   if(getenv("SMGP_TEAM_TRACE"))fprintf(stderr,"[SMGP TEAM] frame=%u object=%04X team=%d wheel=%d boost=%.6f gap=%.6f paint=%08X,%08X,%08X,%08X fade=%u\n",tick,g.id,g.team,g.wheel,g.zoom,g.z,paint[0],paint[1],paint[2],paint[3],white);
   if(getenv("SMGP_MOTION_TRACE"))fprintf(stderr,"[SMGP GP2 SOURCE] frame=%u object=%04X angle=%d frames=%zu phase=%d source=%d,%d origin=%d,%d tread=%08X\n",tick,g.id,g.angle,gp2_cars[g.angle].size(),g.wheel%int(gp2_cars[g.angle].size()),aw,ah,s.x,s.y,genesis_dac_cram_to_argb(g_machine.vdp.cram[(g.palette*16+10)&63],GENESIS_DAC_NORMAL));
   if(g.team>=0&&white==255)if(const char*dir=getenv("SMGP_PAINT_EXPORT_DIR")){
    static std::set<std::string>saved;std::string name="team"+std::to_string(g.team)+"-angle"+std::to_string(g.angle);
    if(saved.insert(name).second)save_rgba(std::filesystem::path(dir)/(name+".rgba"),asset,aw,ah);
   }
  }
  if(smooth_zoom&&!game_index&&!gp2_import&&g.angle>=0&&g.angle<5){
    bool slight=(g.angle==1||g.angle==3)&&std::abs(g.steer)<4&&g.z>18;
    const auto&a=slight?arcade_slight[g.angle==3]:arcade_cars[g.angle];std::array<uint32_t,4> paint{};
    if(a.width){
    // The maximum-pose material roles are CRAM 8/9/6/7 in every native view.
    // Read the live palette directly: staged mip texels are not a fade meter.
    unsigned white=scene_fade(&g_machine.vdp);
    const unsigned roles[]={8,9,6,7};for(int i=0;i<4;i++)paint[i]=genesis_dac_cram_to_argb(g_machine.vdp.cram[g.palette*16+roles[i]],GENESIS_DAC_NORMAL);
    if(g.team>=0)paint=team_paint(g.team,white);
    std::string paint_name="angle"+std::to_string(g.angle);
    for(auto c:paint){char hex[16];snprintf(hex,sizeof(hex),"-%08X",c);paint_name+=hex;}
    const char*export_dir=getenv("SMGP_LIVERY_EXPORT_DIR");static std::set<std::string> exported_paint;
    bool save=export_dir&&exported_paint.insert(paint_name).second;
    if(save)png_write_argb((std::string(export_dir)+"/"+paint_name+"-native.png").c_str(),asset.data(),aw,ah,aw);
    aw=a.width;ah=a.height;colour_arcade(a,asset,paint,white,g.wheel);double unit=56.0/ah;
    if(g.team>=0&&white==255)if(const char*dir=getenv("SMGP_PAINT_EXPORT_DIR")){
     static std::set<std::string> saved;std::string name="team"+std::to_string(g.team)+"-angle"+std::to_string(g.angle);
     if(saved.insert(name).second)save_rgba(std::filesystem::path(dir)/(name+".rgba"),asset,aw,ah);
    }
    if(save)png_write_argb((std::string(export_dir)+"/"+paint_name+"-arcade.png").c_str(),asset.data(),aw,ah,aw);
    if(getenv("SMGP_LIVERY_TRACE"))fprintf(stderr,"[SMGP LIVERY] frame=%u object=%04X angle=%d paint=%08X,%08X,%08X,%08X\n",tick,g.id,g.angle,paint[0],paint[1],paint[2],paint[3]);
    dst=vehicle_rect(offset_width+g.from_x,g.from_foot,aw*unit*g.scale,56*g.scale);
    if(getenv("SMGP_TEAM_TRACE"))fprintf(stderr,"[SMGP TEAM] frame=%u object=%04X team=%d wheel=%d boost=%.6f gap=%.6f paint=%08X,%08X,%08X,%08X fade=%u\n",tick,g.id,g.team,g.wheel,g.zoom,g.z,paint[0],paint[1],paint[2],paint[3],white);
    imported=true;
   }
  }
  int prop_kind=0;
  if(smooth_zoom&&g.id>=(int)profile().props&&!g.architecture&&g.tree<0){
   prop_kind=arcade_prop_kind(g.signature);
  }
  const SweepProp*sweep_art=nullptr;
  if(!prop_kind&&smooth_zoom&&g.id>=(int)profile().props&&!g.architecture&&g.tree<0){
   const char*option=getenv("SMGP_ARCADE_PROPS");
   auto swept=sweep_props.find(g.signature|(unsigned(game_index)<<31));
   if(!(option&&option[0]=='0')&&swept!=sweep_props.end()&&swept->second.kind==3)sweep_art=&swept->second;
  }
  if(prop_kind){
   // Read-only analysis of the live native source: board/stack colour and
   // chevron direction. The arcade source replaces the drawing only.
   std::map<uint32_t,unsigned> accents;double top_x=0,mid_x=0;unsigned top_n=0,mid_n=0;int y0=ah,y1=-1;
   for(int y=0;y<ah;y++)for(int x=0;x<aw;x++){uint32_t p=asset[size_t(y)*aw+x];if(!(p>>24))continue;int r=(p>>16)&255,gg=(p>>8)&255,b=p&255;if(r>150&&gg>150&&b<110){y0=std::min(y0,y);y1=std::max(y1,y);}}
   for(int y=0;y<ah;y++)for(int x=0;x<aw;x++){
    uint32_t p=asset[size_t(y)*aw+x];if(!(p>>24))continue;int r=(p>>16)&255,gg=(p>>8)&255,b=p&255;
    bool yellow=r>150&&gg>150&&b<110;
    if(yellow&&y1>y0){double t=double(y-y0)/(y1-y0);if(t<.2){top_x+=x;top_n++;}else if(std::abs(t-.5)<.1){mid_x+=x;mid_n++;}}
    else if(std::max({r,gg,b})-std::min({r,gg,b})>70)++accents[p&0xffffffu];
   }
   unsigned white=scene_fade(&g_machine.vdp);
   uint32_t accent=0;if(!accents.empty())accent=std::max_element(accents.begin(),accents.end(),[](auto&a,auto&b){return a.second<b.second;})->first;
   unsigned key=g.signature|(unsigned(game_index)<<31);
   if(white==255&&accent)prop_accent[key]=accent;else if(prop_accent.count(key))accent=prop_accent[key];
   int ar=(accent>>16)&255,ag=(accent>>8)&255,ab=accent&255;
   bool red=ar>ag+40&&ar>ab+40,green=ag>ar+40&&ag>ab+40;
   // World stacks keep the qualified arcade colour per mapping (red/green).
   if(prop_kind==2&&!game_index){red=g.signature==0x11452;green=!red;}
   // GP II: runs of stacks mix blue (cartridge colour on arcade shading), red
   // and green (arcade). SMGP_STACK_MIX=0 keeps the cartridge colour only.
   if(prop_kind==2&&game_index){
    const char*mix=getenv("SMGP_STACK_MIX");
    static uint32_t blue=0xff2424d8u;
    if(ab>ar+40&&ab>ag+40)blue=0xff000000u|accent;
    if(!(mix&&mix[0]=='0')){
     int colour=stack_colour(g.world,g.side);red=colour==1;green=colour==2;
     if(colour==0){accent=blue&0xffffffu;ar=(accent>>16)&255;ag=(accent>>8)&255;ab=accent&255;}
    }
   }
   int which=prop_kind==1?(red?PROP_CHEVRON_RED:PROP_CHEVRON_BLUE):(green?PROP_TYRES_GREEN:PROP_TYRES_RED);
   const auto&a=arcade_props21[which];
   if(a.art.width&&a.unit>0){
    std::vector<uint32_t> art=a.art.pixels;int w=a.art.width,h=a.art.height;
    if(prop_kind==1&&top_n&&mid_n&&mid_x/mid_n<top_x/top_n)
     for(int y=0;y<h;y++)std::reverse(art.begin()+size_t(y)*w,art.begin()+size_t(y+1)*w);
    if(prop_kind==2&&!red&&!green&&accent){
     // Arcade stacks exist in red and green only; other cartridge stacks keep
     // their own live colour on the arcade shading (red ramp -> accent ramp).
     int ref=1;for(auto p:art)if(p>>24){int r=(p>>16)&255,gg=(p>>8)&255,b=p&255;if(r-std::max(gg,b)>40)ref=std::max(ref,r);}
     for(auto&p:art)if(p>>24){int r=(p>>16)&255,gg=(p>>8)&255,b=p&255;if(r-std::max(gg,b)<=40)continue;double k=double(r)/ref;int base=std::min(gg,b);
      p=(p&0xff000000u)|(uint32_t(std::min(255.0,ar*k+base))<<16)|(uint32_t(std::min(255.0,ag*k+base))<<8)|uint32_t(std::min(255.0,ab*k+base));}
    }
    if(white<255)for(auto&p:art)if(p>>24)p=paint_mix(0xff000000u,p,white);
    // Arcade size law, measured: screen px = units * depth, ground line
    // 170 + 42.38*depth; ours: ground line 172.6 + 50*scale. Same size at the
    // same ground line => MD px per arcade source px = unit*50/42.38*scale.
    // The road-side edge stays on the cartridge object's road-side edge.
    double px=a.unit*(50.0/42.38)*g.scale,W=w*px,H=h*px;
    double foot=g.from_foot+(g.y+g.h)*g.scale,left=offset_width+g.from_x+g.x*g.scale,right=left+g.w*g.scale;
    dst={float(g.side<0?right-W:left),float(foot-H),float(W),float(H)};
    // Arcade placement (owner 0.21aa/0.21ab): the board/stack starts just
    // after the kerb, on the run-off, its road-side edge on that line. The
    // kerb is measured live on each course's road material (course_material).
    if(kerb_props_enabled()){
     double kerb=kerb_units()+prop_kerb_gap,edge=offset_width+g.road_x+g.side*kerb*g.scale;
     dst.x=float(g.side<0?edge-W:edge);
     if(getenv("SMGP_KERB_TRACE")){double q=std::max(0.0,g.scale*101-6);fprintf(stderr,"[SMGP KERB] frame=%u id=%04X side=%d q=%.2f scale=%.4f road_x=%.2f center=%.2f foot=%.2f edge=%.2f W=%.2f\n",tick,g.id,g.side,q,g.scale,g.road_x,present_center(std::min(q,119.0)),foot,edge-offset_width,W);}
    }
    asset=std::move(art);aw=w;ah=h;imported=true;
   }
  }
  else if(sweep_art){
   // Swept arcade equivalent: same width, foot and road-side edge as the
   // cartridge drawing it replaces (no measured world size for these).
   const auto&a=sweep_art->art;double unit=g.w/a.width;unsigned white=scene_fade(&g_machine.vdp);
   aw=a.width;ah=a.height;asset=a.pixels;
   if(sweep_art->flip)for(int y=0;y<ah;y++)std::reverse(asset.begin()+size_t(y)*aw,asset.begin()+size_t(y+1)*aw);
   if(white<255)for(auto&p:asset)if(p>>24)p=paint_mix(0xff000000u,p,white);
   dst={(float)(offset_width+g.from_x+g.x*g.scale),(float)(g.from_foot+(g.y+g.h-ah*unit)*g.scale),(float)(aw*unit*g.scale),(float)(ah*unit*g.scale)};
   imported=true;
  }
  else if(smooth_zoom&&!game_index&&g.id>=(int)profile().props){
   auto found=arcade_props.find(g.signature);
   if(found!=arcade_props.end()&&found->second.width){
    const auto&a=found->second;double unit=g.w/a.width;
    unsigned white=scene_fade(&g_machine.vdp);
    aw=a.width;ah=a.height;asset=a.pixels;
    if(white<255)for(auto&p:asset)if(p>>24)p=paint_mix(0xff000000u,p,white);
    dst={(float)(offset_width+g.from_x+g.x*g.scale),(float)(g.from_foot+(g.y+g.h-ah*unit)*g.scale),(float)(aw*unit*g.scale),(float)(ah*unit*g.scale)};
    imported=true;
   }
  }
  // Scenery exits on its perspective ray. A lateral animation at gap zero
  // also swept the tunnel ceiling fittings into the corners of the screen.
  if(dst.x+dst.w<=0||dst.x>=width||dst.y+dst.h<=65||dst.y>=224)continue;
  if(getenv("SMGP_QA_INPUT_ONLY"))if(const char*directory=getenv("SMGP_QA_PROP_CATALOG_DIR"))if(g.angle<0&&g.tree<0&&scene_fade(&g_machine.vdp)==255){
   // QA catalogue: one native source per prop mapping, for matching arcade art.
   static std::set<unsigned> saved;
   if(saved.insert(g.signature|(game_index<<31)).second){char name[96];snprintf(name,sizeof(name),"/g%d-map%05X-%dx%d-arc%d.png",game_index+1,g.signature,aw,ah,imported);png_write_argb((std::string(directory)+name).c_str(),asset.data(),aw,ah,aw);}
  }
  if(getenv("SMGP_QA_INPUT_ONLY"))if(const char*directory=getenv("SMGP_QA_DRAW_EXPORT_DIR")){
   const char*identity=getenv("SMGP_QA_EXPORT_OBJECT"),*start=getenv("SMGP_QA_EXPORT_START");
   unsigned actor=identity?unsigned(strtoul(identity,nullptr,16)):(game_index?0xdd10:0xb080);
   if(g.id==int(actor)&&(!start||tick>=unsigned(strtoul(start,nullptr,10)))){
    char name[64];snprintf(name,sizeof(name),"/source-%05u.png",tick);png_write_argb((std::string(directory)+name).c_str(),asset.data(),aw,ah,aw);
   }
  }
  // X-board artwork is point sampled. Fractional coverage is resolved once
  // by the existing 3x target; pre-blurring a mip and then bilinear sampling
  // it erased tyre bands, lettering and posts even at large on-screen sizes.
  SDL_Rect src{0,0,aw,ah};SDL_UpdateTexture(sprite,&src,asset.data(),aw*4);
  SDL_SetTextureScaleMode(sprite,SDL_ScaleModeNearest);
  bool portal_clipped=g.portal&&tunnel_phase()==1&&portal_surface.size()==(size_t)width*height;
  // GP II exit (phase 3): the wall and its opening are the cartridge's own B
  // raster, at the native instant. The enlarged structure jambs are placed on
  // the delayed presentation clock with the host zoom curve, so near the mouth
  // they stood up to 40 px inside the authored opening and narrowed it; the
  // opening then snapped to full width when the cartridge left the tunnel.
  // Draw these jambs only over the authored wall, never over the opening.
  bool exit_clipped=!portal_clipped&&smooth_zoom&&game_index&&g.architecture&&tunnel_phase()==3&&road_pixels.size()==(size_t)width*height;
  if(exit_clipped){
   scenery_alpha.resize((size_t)width*height);
   for(int y=0;y<height;y++)for(int x=0;x<width;x++){
    uint32_t under=y>=65&&road_rows[y].valid?road_pixels[size_t(y)*width+x]:background_pixels[size_t(y)*stride+x];
    scenery_alpha[size_t(y)*width+x]=(under&0xffffffu)==0?0xffffffffu:0x00ffffffu;
   }
   SDL_UpdateTexture(scenery_mask,nullptr,scenery_alpha.data(),width*4);
   portal_clipped=true;
  }
  else if(portal_clipped){
   // The front material is the brightest opaque texel of this original
   // jamb, read live with its palette/fade. This is exact material identity,
   // not a screen-colour histogram or a terrain-derived wall guess.
   uint32_t material=0;unsigned light=0;
   for(auto pixel:asset)if(pixel>>24){unsigned l=((pixel>>16)&255)+((pixel>>8)&255)+(pixel&255);if(!material||l>light){material=pixel;light=l;}}
   scenery_alpha.resize((size_t)width*height);unsigned covered=0;
   for(size_t n=0;n<scenery_alpha.size();n++){bool on=material&&portal_surface[n]==material;scenery_alpha[n]=on?0xffffffffu:0x00ffffffu;covered+=on;}
   SDL_UpdateTexture(scenery_mask,nullptr,scenery_alpha.data(),width*4);
   if(getenv("SMGP_MOTION_TRACE"))fprintf(stderr,"[SMGP PORTAL SURFACE] frame=%u object=%04X material=%08X facade_pixels=%u\n",tick,g.id,material,covered);
   if(getenv("SMGP_QA_INPUT_ONLY"))if(const char*directory=getenv("SMGP_QA_PORTAL_MASK_DIR")){
    static unsigned saved=~0u;if(saved!=tick){saved=tick;char name[64];snprintf(name,sizeof(name),"/facade-%05u.png",tick);png_write_argb((std::string(directory)+name).c_str(),portal_surface.data(),width,height,width);}
   }
  }
  bool crest_clipped=g.angle>=0&&g.ground_clip<224&&!(getenv("SMGP_QA_INPUT_ONLY")&&getenv("SMGP_QA_DISABLE_CREST"));
  if(g.angle>=0&&getenv("SMGP_PROJECTION_TRACE"))fprintf(stderr,"[SMGP CREST DRAW20] frame=%u object=%04X clip=%.6f draw=%.6f,%.6f,%.6f,%.6f hidden=%d\n",tick,g.id,g.ground_clip,dst.x,dst.y,dst.w,dst.h,crest_clipped&&dst.y>=g.ground_clip);
  if(crest_clipped&&dst.y>=g.ground_clip)continue;
  if(crest_clipped){
   // Fractional, coverage-weighted clipping after the common GPU transform.
   // Preserve the roof while it is visible above the crest; no whole-car pop.
   scenery_alpha.resize(size_t(width)*height);
   for(int y=0;y<height;y++){
    unsigned a=unsigned(std::clamp(g.ground_clip-y,0.0,1.0)*255+.5);
    for(int x=0;x<width;x++)scenery_alpha[y*width+x]=(a<<24)|0xffffffu;
   }
   SDL_UpdateTexture(scenery_mask,nullptr,scenery_alpha.data(),width*4);
  }
  if(!portal_clipped)mark_crisp_interior(asset,aw,ah,dst,width,height,mask_scenery&&!g.architecture&&g.angle<0,g.angle>=0,crest_clipped?g.ground_clip:224);
  if(g.angle>=0)dst.y+=float(g.travel*g.scale*g.zoom);
  if(g.angle>=0&&!crest_clipped){SDL_RenderCopyExF(r,sprite,&src,&dst,g.roll,nullptr,SDL_FLIP_NONE);}
  else if(crest_clipped||portal_clipped||(mask_scenery&&!g.architecture)){
   // Mask AFTER the fractional GPU transform. Erasing a whole source texel
   // from its rounded destination centre caused large zoomed chunks to blink
   // along walls. Both targets use the same 3x coverage grid.
   SDL_SetRenderTarget(r,scenery_target);SDL_RenderSetScale(r,coverage_scale,coverage_scale);
   SDL_RenderSetViewport(r,nullptr);SDL_RenderSetClipRect(r,nullptr);
   SDL_SetRenderDrawColor(r,0,0,0,0);SDL_RenderClear(r);
   SDL_RenderSetClipRect(r,&road_clip);SDL_RenderCopyF(r,sprite,&src,&dst);
   SDL_RenderCopy(r,scenery_mask,nullptr,&full);
   SDL_SetRenderTarget(r,target);SDL_RenderSetScale(r,coverage_scale,coverage_scale);
   SDL_RenderSetViewport(r,nullptr);SDL_RenderSetClipRect(r,&road_clip);
   SDL_RenderCopy(r,scenery_target,nullptr,&full);
  }else SDL_RenderCopyF(r,sprite,&src,&dst);++draws;
  if(portal_clipped){
   for(size_t n=0;n<scenery_alpha.size();n++)scenery_alpha[n]=tunnel_occlusion[n]?0x00ffffffu:0xffffffffu;
   SDL_UpdateTexture(scenery_mask,nullptr,scenery_alpha.data(),width*4);
  }
  if(smooth_zoom&&g.angle>=0&&g.spark>0&&g.spark<=spark_cycle.size()){
   const auto&phase=spark_cycle[int(g.spark)-1];const auto&a=phase.image;if(a.width){
   auto pixels=a.pixels;unsigned white=scene_fade(&g_machine.vdp);
   if(white<255)for(auto&p:pixels)if(p>>24)p=paint_mix(0xff000000u,p,white);
   // This native burst was emitted beside a five-pixel rear mip. Convert
   // its measured zoom and foot anchor into the maximum-source world unit.
   double unit=56*g.scale*g.zoom/5,scale=phase.scale*unit;
   SDL_FRect effect{float(dst.x+dst.w*.5+phase.dx*unit-a.width*scale*.5),float(dst.y+dst.h+phase.dy*unit-a.height*scale),float(a.width*scale),float(a.height*scale)};
   auto filtered=minify(pixels,a.width,a.height,scale);
   SDL_Rect area{0,0,filtered.w,filtered.h};SDL_UpdateTexture(sprite,&area,filtered.pixels.data(),filtered.w*4);
   if(crest_clipped){
    SDL_SetRenderTarget(r,scenery_target);SDL_RenderSetScale(r,coverage_scale,coverage_scale);
    SDL_RenderSetViewport(r,nullptr);SDL_RenderSetClipRect(r,nullptr);
    SDL_SetRenderDrawColor(r,0,0,0,0);SDL_RenderClear(r);
    SDL_RenderSetClipRect(r,&road_clip);
    SDL_RenderCopyF(r,sprite,&area,&effect);SDL_RenderCopy(r,scenery_mask,nullptr,&full);
    SDL_SetRenderTarget(r,target);SDL_RenderSetScale(r,coverage_scale,coverage_scale);
    SDL_RenderSetViewport(r,nullptr);SDL_RenderSetClipRect(r,&road_clip);SDL_RenderCopy(r,scenery_target,nullptr,&full);
   }else SDL_RenderCopyF(r,sprite,&area,&effect);++draws;
   if(getenv("SMGP_MOTION_TRACE"))fprintf(stderr,"[SMGP SPARK] frame=%u object=%04X phase=%.6f size=%.6f game=%d original_scale=%.9f\n",tick,g.id,g.spark,double(effect.w),game_index,phase.scale);
   }
  }
  if(crest_clipped){
   for(size_t n=0;n<scenery_alpha.size();n++)scenery_alpha[n]=tunnel_occlusion[n]?0x00ffffffu:0xffffffffu;
   SDL_UpdateTexture(scenery_mask,nullptr,scenery_alpha.data(),width*4);
  }
  if(getenv("SMGP_MOTION_TRACE"))fprintf(stderr,"[SMGP WORLD] frame=%u object=%04X gap=%.6f scale=%.6f draw=%.6f,%.6f,%.6f,%.6f map=%X source=%d,%d arcade=%d\n",tick,g.id,g.z,g.scale,dst.x,dst.y,dst.w,dst.h,g.signature,aw,ah,imported);
 }
 if(player_arcade()){
  SDL_SetTextureScaleMode(sprite,SDL_ScaleModeNearest);
  unsigned white=scene_fade(&g_machine.vdp);
  int team=championship()?team_for_actor(profile().player):-1;std::array<uint32_t,4>paint{};
  if(team>=0)paint=team_paint(team,white);else{
   const unsigned roles[]={8,9,6,7};for(int i=0;i<4;i++)paint[i]=genesis_dac_cram_to_argb(g_machine.vdp.cram[roles[i]],GENESIS_DAC_NORMAL);
  }
  // A final builder epoch may upload two ticks after the pause flag. Freeze
  // the pose actually displayed, rather than rebuilding suspension from that
  // late epoch while the game is already paused.
  if(!rw(profile().pause)||!chassis.valid){
   double lateral=(int16_t)rw(profile().player+18);
   chassis.distance=present_phase();chassis.speed=tick-observed_stamp<=3?observed_velocity:0;
   if(!chassis.valid){chassis=ChassisState{};chassis.valid=true;chassis.stamp=tick;chassis.distance=present_phase();chassis.speed=tick-observed_stamp<=3?observed_velocity:0;}
   // The arcade updates its vehicle controller on alternate VBlanks. Keep
   // the original wheel dead zone, asymmetric limits, selector and bob clock.
   // The MD cartridge has no X-board tyre-force/slip state: the input bridge
   // below supplies a bounded, damped slip estimate, isolated from road-centre
   // changes and impacts. It never changes the cartridge's steering/physics.
   if(tick-chassis.stamp>=2){
    chassis.stamp=tick;chassis.wheel=(int8_t)g_ram[game_index?0xfc2c:0x910a];
    int steer=std::clamp(int(std::lround(chassis.wheel*72.0/112.0)),-72,72);
    int hand=steer<=-4?std::max(-56,steer+4):steer>4?std::min(48,steer-4):0;
    chassis.yaw=int(std::floor(hand/8.0));
    double target=-std::copysign(std::max(0,std::abs(steer)-16)*14.0,steer)*std::clamp(chassis.speed/.5,0.0,1.0);
    int heading=int(chassis.slip/64)*64;
    chassis.slip+=(target-chassis.slip)*.375;
    if(std::abs(chassis.slip)<1)chassis.slip=0;
    // Native RAM/SAT captures confirm heading -> selector -> body,
    // one 30 Hz step each. Read the preceding heading for this selector.
    unsigned index=std::min(std::abs(heading)>>7,3)|(heading<0?4:0);
    chassis.plane=chassis.pending_plane;
    chassis.pending_plane=full_selector[index]/2-3;
    chassis.lateral_speed=chassis.slip;
    // World: offroad word 9208 selects the kerb wheel pose. GP II: FC58 is the
    // surface drag written by the player routine (130FC..13360) after it compares
    // |lateral| (D9B2) with per-track edge limits (FC96/FC98): 0 on the road,
    // negative on the kerb (-40) and grass (-80/-240). Read-only. Same side rule.
    // FC58 alone only turns on when the wheel LEAVES the kerb (|lat| >= FC96-0x30).
    // Screen measurement (kerbframes20): the kerb inner edge reaches the left wheel
    // at |lat| ~200 and the red/white stripes are under it at ~220. The first try
    // (FC9A-0x18 = 192, the rival-car test) raised the wheel before it touched.
    // Enter at the kerb inner edge FC9A (216) and leave 8 units below it: the
    // hysteresis stops the pose toggling while the car hovers at the edge.
    // FC58 (the cartridge's own surface drag) always counts.
    bool surface;
    if(game_index){
     int edge=rw(0xfc9a)>0x40?int(rw(0xfc9a)):0,mag=int(std::abs(lateral));
     if(!edge)chassis.kerb_edge=false;else if(mag>=edge)chassis.kerb_edge=true;else if(mag<edge-8)chassis.kerb_edge=false;
     surface=(int16_t)rw(0xfc58)<0||chassis.kerb_edge;
    }else surface=rw(0x9208)!=0;
    chassis.contact=surface?(lateral<0?0:1):2;
    if(chassis.speed>.0001)chassis.clock=(chassis.clock+0xc0)&0xffff;
    chassis.bob=1-((chassis.clock>>8)&1);
   }
   if(getenv("SMGP_QA_INPUT_ONLY")){
    if(const char*plane=getenv("SMGP_QA_COCKPIT_PLANE"))chassis.plane=std::clamp(atoi(plane),-3,3);
    if(const char*contact=getenv("SMGP_QA_COCKPIT_CONTACT"))chassis.contact=std::clamp(atoi(contact),0,2);
   }
   std::array<double,8> target_geometry;int n=0;
   for(const char*name:{"wheelL","wheelR","armL","armR"}){const auto&p=full_geometry[chassis.contact][chassis.plane+3].at(name);target_geometry[n++]=p.dx;target_geometry[n++]=p.y;}
   if(!chassis.geometry_valid){chassis.geometry=chassis.target_geometry=target_geometry;chassis.geometry_valid=true;}
   if(target_geometry!=chassis.target_geometry){chassis.from_geometry=chassis.geometry;chassis.target_geometry=target_geometry;chassis.tween=2;}
   // Original 8898..89C4 builds the midpoint SAT between old/new physical
   // origins, followed by the complete pose. Preserve that two-picture move.
   if(chassis.tween){double t=chassis.tween==2?.5:1;for(int i=0;i<8;i++)chassis.geometry[i]=chassis.from_geometry[i]+(chassis.target_geometry[i]-chassis.from_geometry[i])*t;--chassis.tween;}
  }
  int wheel=chassis.wheel;
  int yaw=chassis.yaw,pose=std::min(7,std::abs(yaw));
  const auto&original=authored_poses[yaw+9];pose=original.hands;
  int phase=(int)std::floor(chassis.distance*.16)&3;
  int bounce=chassis.bob;
  std::vector<std::string> player_parts={"wheelL"+std::to_string(original.left)+"-"+std::to_string(phase),"wheelR"+std::to_string(original.right)+"-"+std::to_string(phase),"armL","armR","fairing","dashboard","hands"+std::to_string(pose)};
  for(const auto&name:player_parts){
   bool broken=rw(game_index?0xfe54:0xfc54)&&rw(profile().player+0x36);
   const char*broken_side=g_ram[game_index?0xfe64:0xfc9c]?"L":"R";
   if(broken&&(name.rfind(std::string("wheel")+broken_side,0)==0||name==std::string("arm")+broken_side)){
    if(getenv("SMGP_PLAYER_TRACE"))fprintf(stderr,"[SMGP BROKEN PART] frame=%u name=%s side=%s\n",tick,name.c_str(),broken_side);
    continue;
   }
   auto full_part=full_player[chassis.plane+3].find(name);
   const auto&part=full_part==full_player[chassis.plane+3].end()?arcade_player.at(name):full_part->second;
   const auto&a=part.image;auto pixels=a.pixels;
   const auto&glass=mirror_glass[chassis.plane+3];
   for(size_t index=0;index<pixels.size();++index){auto&p=pixels[index];if(!(p>>24))continue;
    if(name=="fairing"&&glass.width==a.width&&glass.height==a.height&&!glass.roles.empty()&&glass.roles[index]){
     // Both original glass tones are a separate, qualified arcade material.
     // A team paint can dye the housing, never the lens or its texture.
     p=paint_mix(0xff000000u,p,white);continue;
    }
    unsigned red=(p>>16)&255,green=(p>>8)&255,blue=p&255;
    // Glass, its blue/white bevels and neutral materials retain their colours.
    // Shell and warm clothing texels use the current team's separate materials.
    if(name=="fairing"&&red>green+20&&red>blue+25){
     int x=int(index%a.width),y=int(index/a.width);
     bool housing=y>=10&&y<=23&&(x<a.width*.28||x>a.width*.72);
     int role=housing?0:2;
     p=paint_mix(paint[role+1],paint[role],std::clamp((red-green)*2u,0u,255u));
    }else if((name.rfind("hands",0)==0||name.rfind("arm",0)==0)&&red>green+20&&red>blue+25){
     p=paint_mix(paint[3],paint[2],std::clamp((red-green)*2u,0u,255u));
    }else p=paint_mix(0xff000000u,p,white);
   }
   if(white==255)if(const char*dir=getenv("SMGP_PAINT_EXPORT_DIR")){
    static std::set<std::string> saved;std::string key="player-team"+std::to_string(team)+"-"+name;
    if(saved.insert(key).second)save_rgba(std::filesystem::path(dir)/(key+".rgba"),pixels,a.width,a.height);
    if(name.rfind("hands",0)!=0){std::string exact="player-team"+std::to_string(team)+"-plane"+std::to_string(chassis.plane)+"-"+name;if(saved.insert(exact).second)save_rgba(std::filesystem::path(dir)/(exact+".rgba"),pixels,a.width,a.height);}
   }
   bool hands=name.rfind("hands",0)==0,mirror=hands&&original.flip;
   double dx=hands?original.dx:part.dx,y=hands?original.y:part.y+(full_part==full_player[chassis.plane+3].end()?15:0);
   std::string geometry_part=name.rfind("wheel",0)==0?name.substr(0,6):name;
   auto placement=full_geometry[chassis.contact][chassis.plane+3].find(geometry_part);
   if(placement!=full_geometry[chassis.contact][chassis.plane+3].end()){
    // Replace the straight/observed anchor with the original ROM geometry.
    const auto&base=geometry_part.rfind("wheel",0)==0?part:full_player[3].at(name);
    int n=geometry_part=="wheelL"?0:geometry_part=="wheelR"?2:geometry_part=="armL"?4:6;
    dx=base.dx+chassis.geometry[n];y=base.y+(geometry_part.rfind("wheel",0)==0?15:0)+chassis.geometry[n+1];
   }
   // Original descriptor origins, crops and handedness. No rim centering,
   // sinusoidal suspension, per-layer sway coefficients or invented roll.
   constexpr double pixel_aspect=.8;
   SDL_FRect dst{float(offset_width+128+dx*pixel_aspect),float(y+bounce),float(a.width*pixel_aspect),float(a.height)};
   SDL_Rect src{0,0,a.width,a.height};SDL_UpdateTexture(sprite,&src,pixels.data(),a.width*4);
   SDL_RenderCopyExF(r,sprite,&src,&dst,0,nullptr,mirror?SDL_FLIP_HORIZONTAL:SDL_FLIP_NONE);++draws;
   if(name=="fairing"&&!glass.roles.empty()){
    mark_original_glass(glass,dst,width,height);
    if(getenv("SMGP_PLAYER_TRACE"))fprintf(stderr,"[SMGP GLASS] frame=%u plane=%d source=%d,%d draw=%.6f,%.6f,%.6f,%.6f texels=%zu fade=%u\n",tick,chassis.plane,a.width,a.height,dst.x,dst.y,dst.w,dst.h,size_t(std::count(glass.roles.begin(),glass.roles.end(),1)),white);
   }
   if(getenv("SMGP_PLAYER_TRACE"))fprintf(stderr,"[SMGP AUTHORED PART] frame=%u yaw=%d name=%s dx=%.6f y=%.6f bob=%d flip=%d\n",tick,yaw,name.c_str(),dx,y,bounce,mirror);
  }
  if(getenv("SMGP_TEAM_TRACE"))fprintf(stderr,"[SMGP CHASSIS] frame=%u authored=%d bob=%d paused=%d plane=%d lateral_speed=%.6f contact=%d surface=%d\n",tick,yaw,bounce,rw(profile().pause)!=0,chassis.plane,chassis.lateral_speed,chassis.contact,game_index?(int16_t)rw(0xfc58):(int)rw(0x9208));
  if(getenv("SMGP_TEAM_TRACE"))fprintf(stderr,"[SMGP PLAYER] frame=%u team=%d steer=%d pose=%d slots=%u window_pixels=%u paint=%08X,%08X,%08X,%08X fade=%u yaw=%d rotation=%d parts=%zu game=%d\n",tick,team,wheel,pose,player_slots,player_window_pixels,paint[0],paint[1],paint[2],paint[3],white,yaw,phase,player_parts.size(),game_index);
 }
 SDL_RenderSetClipRect(r,nullptr);
 // No veil overlay: the widescreen margins keep the real scenery (owner
 // rejected dark bands before the portal that vanished on exit).
 if(foreground.size()==(size_t)width*height){SDL_UpdateTexture(front,nullptr,foreground.data(),width*4);SDL_RenderCopy(r,front,nullptr,&full);}
 composed.resize((size_t)stride*height);
 // Resolve 3x coverage only after GPU transforms. Reading a native-sized
 // target first discarded the fractional poses before the final CRT output.
 int gw=width*coverage_scale,gh=height*coverage_scale;gpu_pixels.resize((size_t)gw*gh);
 int result=SDL_RenderReadPixels(r,nullptr,SDL_PIXELFORMAT_ARGB8888,gpu_pixels.data(),gw*4);
 SDL_SetRenderTarget(r,old);SDL_RenderSetLogicalSize(r,lw,lh);SDL_RenderSetScale(r,sx,sy);
 SDL_RenderSetViewport(r,&viewport);SDL_RenderSetClipRect(r,clipped?&clip:nullptr);
 if(result!=0)return recover_native(input,stride,width,height);
 for(int y=0;y<height;y++)for(int x=0;x<width;x++){
  unsigned red=0,green=0,blue=0;
  for(int yy=0;yy<coverage_scale;yy++)for(int xx=0;xx<coverage_scale;xx++){
   uint32_t p=gpu_pixels[(y*coverage_scale+yy)*gw+x*coverage_scale+xx];red+=(p>>16)&255;green+=(p>>8)&255;blue+=p&255;
  }
  constexpr unsigned count=coverage_scale*coverage_scale;
  composed[y*stride+x]=crisp_interior[y*width+x]?gpu_pixels[(y*coverage_scale+coverage_scale/2)*gw+x*coverage_scale+coverage_scale/2]:0xff000000u|((red/count)<<16)|((green/count)<<8)|(blue/count);
 }
 if(ending_active){
  if(ending_is_retire&&!last_race_background.empty()){ending_background=last_race_background;ending_width=last_race_width;composed=ending_background;}
  else{ending_background=composed;ending_width=width;}
  ending_overlay(composed,stride,width,height);
 }else if(!rw(game_index?0xfe1c:0xfc80)){last_race_background=composed;last_race_width=width;}
 compose_key=frame;
 // GP II tunnel roof, WIDESCREEN margins only (phases 1-3, rows 65-99). The
 // flat grey (RGB 206) of the roof blanking band was extended into the margins
 // by the widened composition, as a light block. The native 256 columns keep
 // the cartridge's own colour (it exists in the unmodified native output too).
 // Only where the native edge of the same row is NOT that grey: when the
 // authored facade itself reaches the edge (the mouth approaching), the
 // margins continue it. Blackening those rows unconditionally left black
 // blocks beside the grey facade on the frames before entering.
 if(game_index&&tunnel_phase()>=1&&tunnel_phase()<=3&&offset_width>0){
  const int native_end=offset_width+256;
  for(int y=65;y<100;y++)for(int side=0;side<2;side++){
   // Read from the B raster of that line (HUD glyphs sit on the edge rows).
   if(facade_edge[y][side])continue;
   for(int x=side?native_end:0;x<(side?width:offset_width);x++){
    uint32_t&p=composed[size_t(y)*stride+x];if((p&0xffffff)==0xcecece)p=0xff000000u;
   }
  }
 }
 static bool reported=false;
 if(!reported||frame%300==0){
  SDL_RendererInfo info{};SDL_GetRendererInfo(r,&info);
  fprintf(stderr,"[SMGP GPU] frame=%llu actors=%zu pieces=%zu draws=%u tracking=%u renderer=%s accelerated=%d\n",
  (unsigned long long)frame,history.size(),pieces.size(),draws,tracked,info.name,(info.flags&SDL_RENDERER_ACCELERATED)!=0);reported=true;
 }
 return composed.data();
}
extern "C" void smgp_scene_shutdown(void){release();}
extern "C" void smgp_scene_formation_overlay(int line,uint32_t*row,int width){
 if(!formation_active||line!=formation_line)return;
 for(int x=0;x<width&&x<1024;x++)if(formation_row[x])row[x]=formation_row[x];
}
extern "C" void smgp_scene_diagnostics(void){
 if(active&&championship()&&getenv("SMGP_TEAM_TRACE"))fprintf(stderr,"[SMGP MIRROR] frame=%u painted_pixels=%u\n",tick,mirror_team_pixels);
 if(active&&getenv("SMGP_MATCH_TRACE"))fprintf(stderr,"[SMGP REPLACEMENT] frame=%u matched=%d selected=%zu native_pixels=%u leaked_pixels=%u unsupported=%u actors=%zu\n",tick,upload_matched,std::count(selected.begin(),selected.end(),true),original_world_pixels,leaked_world_pixels,unsupported_slots,groups.size());
 const char *at=getenv("SMGP_LAYER_FRAME"),*dir=getenv("SMGP_LAYER_DIR");
 if(!at||!dir||tick!=(unsigned)strtoul(at,nullptr,10)||layer_native.empty())return;
 std::string base=dir;
 png_write_argb((base+"/native.png").c_str(),layer_native.data(),256,224,256);
 png_write_argb((base+"/plane-b.png").c_str(),layer_b.data(),256,224,256);
 std::vector<uint32_t> atlas(512*512);for(int y=0;y<512;y++)for(int x=0;x<512;x++)atlas[y*512+x]=plane_b(&g_machine.vdp,x,y);
 png_write_argb((base+"/atlas-b.png").c_str(),atlas.data(),512,512,512);
 int fw=256+offset_width*2;
 if(foreground.size()==(size_t)fw*224)png_write_argb((base+"/foreground.png").c_str(),foreground.data(),fw,224,fw);
 if(foreground.size()==(size_t)fw*224){std::vector<uint32_t> mask(foreground.size());for(size_t i=0;i<mask.size();i++)mask[i]=foreground[i]?0xffffffff:0xff000000;png_write_argb((base+"/foreground-mask.png").c_str(),mask.data(),fw,224,fw);}
 if(road_pixels.size()==(size_t)fw*224)png_write_argb((base+"/road.png").c_str(),road_pixels.data(),fw,224,fw);
 FILE *f=fopen((base+"/ram.bin").c_str(),"wb");if(f){fwrite(g_ram,1,65536,f);fclose(f);}
}
extern "C" void smgp_scene_observe(uint32_t pc){
 if(game_index&&pc==0xc5be){unsigned cue=g_cpu.D[0]&255;if(cue!=0x86){result_retire=false;}}
 if(game_index&&pc==0x11c5e&&smooth_zoom){
  result_retire=true;load_arcade();
  if(shared_retire.width){
   if(!ending_active){ending_age=0;ending_background=last_race_background;ending_width=last_race_width;ending_anchor_x=(256-shared_retire.width)/2;ending_anchor_y=128-shared_retire.height/2;}
   ending_banner=shared_retire;ending_is_retire=true;ending_active=true;
  }
  if(getenv("SMGP_PLAYER_TRACE"))fprintf(stderr,"[SMGP RETIRE NATIVE] frame=%u pc=11C5E signed_rank=%d\n",tick,(int16_t)rw(0xff5a));
 }
 if((!game_index&&pc==0x7b76)||(game_index&&pc==0x13196)){tree_collision();prop_collision();}
 if(game_index){
  if(pc==0xd3a4){formation_building.fill(-1);++formation_serial;building_stamp=tick;building_owner.fill(0);building_mirror_owner.fill(0);objects_ready=false;++build_serial;return;}
  if(pc==0xd3e2){snapshot_world();objects_ready=true;return;}
  if(pc==0xd49c||pc==0xd588){accepted_piece(g_cpu.A[1],g_cpu.A[6]);return;}
  if(pc==0xd434||pc==0xd55c||pc==0xd4e4||pc==0xd5d0)seal_scene();return;
 }
 if(pc==0xd8e){building_stamp=tick;building_owner.fill(0);building_mirror_owner.fill(0);objects_ready=false;++build_serial;return;}
 if(pc==0xe86){snapshot_world();objects_ready=true;return;}
 if(pc==0xe00||pc==0xe6c){accepted_piece(g_cpu.A[3],g_cpu.A[6]);return;}
 if(pc==0xed4||pc==0xee8)seal_scene();
}
namespace {
void publish_scene(const UploadedScene&s){
 {
  // Keep the capture's VBlank epoch through the SAT upload. Recognition can
  // occur after 1, 3 or 5 presentation ticks; that is not the simulation step.
  // Camera, road and actors all use this one source epoch, for both velocity
  // and bounded prediction. Never replace it with the recognition timestamp.
  double dt=std::max(1u,s.stamp>=camera_stamp?s.stamp-camera_stamp:7u);
  for(int i=0;i<120;i++)center_velocity[i]=world_serial&&dt<=6?std::clamp((s.center[i]-world_center[i])/dt,-8.0,8.0):0;
  for(int i=0;i<96;i++)ground_velocity[i]=world_serial&&dt<=6?std::clamp((s.ground[i]-world_ground[i])/dt,-2.0,2.0):0;
  double delta=s.phase-camera_phase,lap=rw(profile().length);
  if(lap>100){while(delta>lap*.5)delta-=lap;while(delta<-lap*.5)delta+=lap;}
  camera_velocity=world_serial&&dt<=6?std::clamp(delta/dt,0.0,3.0):0;
  panorama_velocity=world_serial&&dt<=6?std::clamp(panorama_difference(s.panorama,panorama_scroll)/dt,-8.0,8.0):0;
  panorama_scroll=s.panorama;
  if(getenv("SMGP_MOTION_TRACE"))fprintf(stderr,"[SMGP EPOCH] frame=%u serial=%u sample=%u previous=%u dt=%.0f speed=%.6f phase=%.6f measured=%u panorama=%.6f\n",tick,s.serial,s.stamp,camera_stamp,dt,camera_velocity,s.phase,s.measured_at,s.panorama);
  camera_phase=s.phase;camera_stamp=s.stamp;camera_measured_at=s.measured_at;
  if(!world_serial)for(int q=0;q<96;q++){
   int shift=0;
   for(int k=0;k<16;k++){
    bool match=true;for(int p=0;p<16;p++)if(((int16_t)romw(profile().stripes+p*192+q*2)!=0)!=((p+k)%16<8)){match=false;break;}
    if(match){shift=k;break;}
   }
   double estimate=1060.240380567/(q+7.035)-9.353729982+(game_index?8:0);
   int n=shift+16*(int)std::round((estimate-shift-.5)/16);
   stripe_distance[q]=std::clamp(estimate,n+.01,n+.99);
  }
  // Preserve the same captured source epoch for every part of the scene.
  auto next_objects=s.objects;
  for(auto&p:next_objects){
   auto old=std::find_if(world_objects.begin(),world_objects.end(),[&](const ObjectPose&o){return o.object==p.object&&o.routine==p.routine;});
   if(p.object>=profile().props||p.architecture){
    // Qualified roadside actors are stationary in world Z. Their relative
    // speed is exactly the camera's, including the first visible generation.
    p.dgap=-camera_velocity;
    // The world Z of a stationary prop is fixed; each snapshot measures it with
    // quantised words. Smooth that estimate over snapshots, so the distance is
    // the smooth camera phase minus a steady Z (no step per snapshot).
    double z=p.gap+p.phase;
    p.zsmooth=old!=world_objects.end()&&std::abs(z-old->zsmooth)<12?old->zsmooth+(z-old->zsmooth)*.5:z;
   }else if(old!=world_objects.end()&&p.stamp>old->stamp&&p.stamp-old->stamp<=6&&std::abs(p.gap-old->gap)<12){
    double dt=p.stamp-old->stamp;p.dgap=std::clamp((p.gap-old->gap)/dt,-2.5,2.5);p.dlateral=std::clamp((p.lateral-old->lateral)/dt,-4.0,4.0);
   }
  }
  world_objects=std::move(next_objects);world_center=s.center;world_ground=s.ground;
  committed_serial=s.serial;++world_serial;
  std::stable_sort(world_objects.begin(),world_objects.end(),[](const ObjectPose&a,const ObjectPose&b){return a.gap<b.gap;});
 }
}

}
extern "C" void smgp_scene_vblank(void){}
