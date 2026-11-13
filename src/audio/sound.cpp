// Host-only audio presentation. Native RAM, sound CPU, chips and music driver
// keep running unchanged. Selected delivery channels are replaced, not layered
// with duplicate MD engines. No audio work runs on the SDL callback.
#include "sound.h"
#include "arcade_board.h"
#include <SDL2/SDL.h>
extern "C" {
#include "genesis_runtime.h"
#include "genesis_machine.h"
#include "audio.h"
#include "audio/ym2612.h"
}
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>
namespace {
int game_index=0;uint64_t tick=0,last_frame=UINT64_MAX;
bool initialized=false,available=false,race=false,was_race=false,paused=false,victory=false;
bool trace=false;unsigned last_impact=0,last_skid=0,mask=63;
double music_position=0;std::vector<int16_t> music;unsigned music_rate=48000;
SmgpArcadeSound arcade;std::vector<float> scratch;std::deque<uint8_t> effects;
std::array<uint8_t,11> packet{};
struct Nearby {double distance,lane;unsigned object;};
unsigned word(unsigned a){return unsigned(g_ram[a&65535])<<8|g_ram[(a+1)&65535];}
uint32_t longword(unsigned a){return word(a)<<16|word(a+2);}
unsigned read8(unsigned a){return (a&0xff0000)==0xff0000?g_ram[a&65535]:(a<0x100000?g_rom[a]:0);}
bool enabled(){const char*p=getenv("SMGP_AUDIO_ORIGINAL");return !(p&&p[0]=='1');}
void event(uint8_t command,const char*name){
 if(effects.size()<16)effects.push_back(command);
 if(trace)fprintf(stderr,"[SMGP ARCADE EVENT] frame=%llu game=%d event=%s command=%02X\n",(unsigned long long)tick,game_index,name,command);
}
void reset_board(){
 arcade.reset();effects.clear();
 // Original reset/PCM/YM setup finishes before its first game packet/NMI.
 float startup[2048];arcade.render(startup,1024,62500);
}
void mix(int16_t*out,size_t frames,double rate){
 if(!available&&!victory)return;
 if(paused){if(race||victory)std::fill_n(out,frames*2,int16_t(0));return;}
 if(victory){
  const double step=double(music_rate)/rate;const size_t length=music.size()/2;
  for(size_t n=0;n<frames;n++){
   size_t a=size_t(music_position);double t=music_position-a;
   for(unsigned ch=0;ch<2;ch++)out[n*2+ch]=a<length?int16_t(music[a*2+ch]+(music[std::min(a+1,length-1)*2+ch]-music[a*2+ch])*t):0;
   music_position+=step;
  }
  return;
 }
 if(!race){if(was_race){reset_board();was_race=false;}return;}
 if(!was_race){reset_board();was_race=true;}
 auto p=packet;
 if(!effects.empty()){p[0]=effects.front();effects.pop_front();}
 const char*solo=getenv("SMGP_QA_INPUT_ONLY")?getenv("SMGP_QA_AUDIO_SOLO"):nullptr;
 if(solo){
  std::fill_n(out,frames*2,int16_t(0));std::string part=solo;
  if(part!="player")p[2]=0;
  if(part!="traffic")for(unsigned n=5;n<11;n++)p[n]=0;
  if(part!="effects")p[0]=0x80;
 }
 arcade.packet(p);scratch.resize(frames*2);arcade.render(scratch.data(),frames,rate);
 int peak=0;unsigned clips=0;
 for(size_t n=0;n<frames*2;n++){
  int value=int(out[n])+int(std::lround(scratch[n]*32768*2.0));peak=std::max(peak,std::abs(value));clips+=value>32767||value< -32768;
  out[n]=int16_t(std::clamp(value,-32768,32767));
 }
 if(trace&&tick%60==0)fprintf(stderr,"[SMGP ARCADE MIX] frame=%llu samples=%zu rate=%.4f mask=%02X peak=%d clipped=%u commands=%llu\n",(unsigned long long)tick,frames,rate,mask,peak,clips,(unsigned long long)arcade.commands_read());
}
void initialize(){
 initialized=true;trace=getenv("SMGP_AUDIO_TRACE")!=nullptr;
 if(!enabled())return;
 char*base=SDL_GetBasePath();std::filesystem::path folder=base?base:".";SDL_free(base);
 if(const char*p=getenv("SMGP_AUDIO_ASSETS"))folder=p;
 else{for(int n=0;n<7&&!std::filesystem::is_directory(folder/"assets/audio18");n++)folder=folder.parent_path();folder/="assets/audio18";}
 available=arcade.load((folder/"arcade-sound.sb17").string());
 SDL_AudioSpec spec{};Uint8*data=nullptr;Uint32 bytes=0;
 if(SDL_LoadWAV((folder/"gp2-victory.wav").string().c_str(),&spec,&data,&bytes)){
  if(spec.format==AUDIO_S16LSB&&spec.channels==2&&spec.freq==48000&&bytes%4==0){music.assign(reinterpret_cast<int16_t*>(data),reinterpret_cast<int16_t*>(data+bytes));music_rate=spec.freq;}
  SDL_FreeWAV(data);
 }
 audio_set_extra_source(mix);
 fprintf(stderr,"[SMGP AUDIO20] arcade=%d victory_frames=%zu native_fallback=%d\n",available,music.size()/2,!available);
}
void diagnostic(int racing){
 if(!trace)return;
 unsigned base=game_index?0xfc20:0x9100;
 fprintf(stderr,"[SMGP AUDIO FRAME] frame=%llu game=%d racing=%d active_engines=%d parameters=",(unsigned long long)tick,game_index,racing,race);
 for(unsigned i=0;i<48;i++)fprintf(stderr,"%02X",g_ram[base+i]);
 fprintf(stderr," mailbox=");for(unsigned i=0x1c00;i<0x1c40;i++)fprintf(stderr,"%02X",g_machine.bus.z80_ram[i]);
 fprintf(stderr," endmail=");for(unsigned i=0x1fa0;i<0x2000;i++)fprintf(stderr,"%02X",g_machine.bus.z80_ram[i]);fprintf(stderr,"\n");
 if(const char*dir=getenv("SMGP_AUDIO_DUMP_DIR"))if(tick==900||tick==1800||tick==3600||tick==4000){
  std::filesystem::create_directories(dir);char name[64];snprintf(name,sizeof(name),"/z80-%05llu.bin",(unsigned long long)tick);
  if(FILE*f=fopen((std::string(dir)+name).c_str(),"wb")){fwrite(g_machine.bus.z80_ram,1,8192,f);fclose(f);}
 }
}
}
extern "C" void smgp_sound_frame(int game,uint64_t frame,int racing){
 game_index=game;tick=frame;if(!initialized)initialize();
 if(last_frame!=UINT64_MAX&&frame<=last_frame){victory=false;music_position=0;was_race=false;effects.clear();last_impact=last_skid=0;}
 last_frame=frame;paused=word(game?0xfe42:0xfc66)!=0;
 // The qualified native driving dispatcher covers both preliminary and
 // Championship races (FC04 is zero in the latter). Menus share the VDP
 // layout but must not start engines or mute the native music channels.
 unsigned page=longword(0xff20),queued=longword(0xff24);
 bool driving=!game||page==0x109d8||(page==0x10a72&&queued==0x109d8);
 race=racing&&!word(game?0xfe1c:0xfc80)&&driving;
 diagnostic(racing);mask=63;
 if(!enabled()){ym2612_set_output_mask(mask);return;}
 if(available&&race){
  // World player: FM2 (special operator frequencies); opponents FM0/1.
  // GP II player: DAC5; opponents FM0/1. Music/PSG stay native.
  mask=game?0x1c:0x38;
  if(!game){
   if(g_machine.bus.z80_ram[0x1e20]&128)mask|=1;
   if(g_machine.bus.z80_ram[0x1e50]&128)mask|=2;
   // World borrows mirror channels only while native music voices are free.
   if(!(g_machine.bus.z80_ram[0x1eb0]&128))mask&=~16u;
   if(!(g_machine.bus.z80_ram[0x1ee0]&128))mask&=~32u;
   // World A9 (skid), BB/BD (contact) and AD (breakage) share FM4.
   // Match the live sequence cursor, not just the occupied channel: other
   // cartridge cues borrowing FM4 must remain audible. These exact ranges
   // come from the qualified Z80 14BC table and were confirmed by native
   // mailbox traces during real contact and declared skid QA.
   auto*z=g_machine.bus.z80_ram;unsigned cursor=z[0x1eb3]|unsigned(z[0x1eb4])<<8;
   if((z[0x1eb0]&128)&&((cursor>=0x1510&&cursor<0x1523)||(cursor>=0x1632&&cursor<0x1637)||(cursor>=0x170b&&cursor<0x171a)))mask&=~16u;
  }else{
   // Z80 00E0/027B: signed 1FF7 identifies a spoken PCM cue, not the
   // looping engine. Keep Senna's cartridge-only advice and overrev effect.
   unsigned pcm=g_machine.bus.z80_ram[0x1ff7];
   if(pcm>=0x90||pcm==4)mask|=32;
   // Original E0000 score: AA skid has instrument pointer 828B;
   // AC collision uses 82F8 on channels 4/5. The Z80 saves that pointer
   // at voice+2A. Suppress only those replaced effects, preserve music.
   for(unsigned a=0x1c40;a<=0x1ca0;a+=0x30){
    auto*z=g_machine.bus.z80_ram;unsigned instrument=z[a+0x2a]|unsigned(z[a+0x2b])<<8;
    if((z[a]&128)&&(instrument==0x828b||instrument==0x82f8||instrument==0x8234)){
     unsigned encoded=z[a+1]&7,channel=(encoded&3)+(encoded&4?3:0);
     if(channel<6)mask&=~(1u<<channel);
    }
   }
  }
  unsigned speed=word(game?0xfc2a:0x9108),rpm=word(game?0xfc24:0x9102),gear=std::min(3u,word(game?0xfc20:0x9100));
  packet={0x00,0,uint8_t(std::clamp(4+int(rpm*(game?123.0/14000:123.0/1500)),4,127)),uint8_t(4|((gear+1)<<5)),127,0,0,0,0,0,0};
  unsigned impact=word(game?0xfe5c:0xfc52)|word(game?0xfe54:0xfc54);
  // GP II 14692..146B8 and World 9B6A..9B8E are the native skid cues.
  unsigned skid=word(game?0xfc32:0x910e);
  if(impact&&!last_impact)event(0xa7,"impact");
  // Original Z80 1691/170E: 01..0F control the continuous PCM tyre effect
  // (descriptor 43, voices 2/3); 00 stops it. 08 is its authored centre pan.
  // 93 instead runs the short two-voice FM score at 4F9B. Repeating that
  // score at brake cadence produced the rejected electronic chirp.
  unsigned tyre=skid&&speed>40?8:0;
  packet[0]=uint8_t(tyre);
  if(tyre!=last_skid&&trace)fprintf(stderr,"[SMGP ARCADE EVENT] frame=%llu game=%d event=skid-%s command=%02X\n",(unsigned long long)tick,game_index,tyre?"start":"stop",tyre);
  last_skid=tyre;
  last_impact=impact;
  unsigned player=game?0xd9a0:0xae00,first=game?0xdd10:0xb080,step=game?80:64,end=game?0xe1c0:0xb440;
  double pos=longword(player+30)/65536.0,length=word(game?0xfc48:0x9206),lane=int16_t(word(player+18));
  std::vector<Nearby> near;
  for(unsigned a=first;a<end;a+=step)if(longword(a)){
   double delta=longword(a+30)/65536.0-pos;if(length>100){delta=std::fmod(delta+length*1.5,length)-length*.5;}
   if(std::abs(delta)<180)near.push_back({delta,double(int16_t(word(a+18)))-lane,a});
  }
  std::sort(near.begin(),near.end(),[](const auto&a,const auto&b){return std::abs(a.distance)<std::abs(b.distance);});
  for(size_t n=0;n<std::min(size_t(3),near.size());n++){
   const auto&a=near[n];
   // Original Z80 0FF1/105B/10D8: byte one is longitudinal range
   // (bit7 = rear), NOT engine pitch; byte two is pan 1..15, NOT volume.
   // Tables 2A39/2AB9/2C39 supply the original Doppler and attenuation;
   // 2CB9 supplies the original left/right pair. Never turn approaching
   // distance into a pan sweep or use lateral sign as front/rear.
   int distance=std::clamp(int(std::lround(std::abs(a.distance)*112/180)),0,127);
   int pan=std::clamp(int(std::lround(8+a.lane/35)),1,15);
   packet[5+n*2]=uint8_t(distance|(a.distance<0?128:0));packet[6+n*2]=uint8_t(pan);
  }
  if(trace&&frame%30==0)fprintf(stderr,"[SMGP ARCADE PACKET] frame=%llu speed=%u rpm=%u gear=%u opponents=%zu data=%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X\n",(unsigned long long)frame,speed,rpm,gear,near.size(),packet[0],packet[1],packet[2],packet[3],packet[4],packet[5],packet[6],packet[7],packet[8],packet[9],packet[10]);
 }else last_impact=last_skid=0;
 ym2612_set_output_mask(mask);
}
extern "C" void smgp_sound_observe(uint32_t pc){
 if(game_index&&pc==0xc5be){
  unsigned cue=g_cpu.D[0]&255;
  if(cue!=g_ram[0xff78]){
   bool play=enabled()&&cue==0x85&&int16_t(word(0xff5a))>=0&&word(0xff5a)<3&&!music.empty();
   victory=play;music_position=0;
   if(trace)fprintf(stderr,"[SMGP VICTORY CUE] frame=%llu cue=%02X rank=%d remix=%d\n",(unsigned long long)tick,cue,int16_t(word(0xff5a)),play);
  }
 }
 if(!trace)return;
 if(!game_index&&pc==0x761dc){
  fprintf(stderr,"[SMGP AUDIO COPY] frame=%llu destination=%06X source=%06X count=%u bytes=",(unsigned long long)tick,g_cpu.A[3]&0xffffff,g_cpu.A[5]&0xffffff,(g_cpu.D[7]&65535)+1);
  for(unsigned i=0;i<std::min(64u,(g_cpu.D[7]&65535)+1);i++)fprintf(stderr,"%02X",read8((g_cpu.A[5]+i)&0xffffff));fprintf(stderr,"\n");
 }else if(game_index&&(pc==0xc5be||pc==0xc5e0||pc==0xc5e6||pc==0xc616||pc==0xc656||pc==0xc66c))
  fprintf(stderr,"[SMGP AUDIO COMMAND] frame=%llu pc=%06X value=%02X aux=%02X\n",(unsigned long long)tick,pc,pc==0xc616?g_ram[0xfea8]:g_cpu.D[0]&255,g_ram[0xfea1]);
}
