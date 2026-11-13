// Original supplied sound program on the existing MIT Z80 and BSD ymfm.
// Independent host-only PCM bus/mixer from the documented register interface.
// No MAME source, objects or runtime dependency are incorporated here.
#include "arcade_board.h"
#include "ymfm_opm.h"
extern "C" {
#include "z80.h"
}
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <vector>
struct SmgpArcadeSound::Impl {
 struct Voice {uint32_t position=0;uint16_t repeat=0;uint8_t page=0,step=0,left=0,right=0,flags=255;};
 struct Event {uint64_t cycle;uint8_t value;};
 struct Iface:ymfm::ymfm_interface {
  Impl&host;explicit Iface(Impl&h):host(h){}
  void ymfm_set_timer(uint32_t n,int32_t clocks)override{host.timer[n]=clocks<0?UINT64_MAX:host.clock+uint32_t(clocks);}
  void ymfm_set_busy_end(uint32_t clocks)override{host.busy=host.clock+clocks;}
  bool ymfm_is_busy()override{return host.clock<host.busy;}
  void ymfm_update_irq(bool value)override{host.irq=value;}
  void expire(unsigned n){m_engine->engine_timer_expired(n);}
 };
 uint64_t clock=0,next_sample=64,timer[2]={UINT64_MAX,UINT64_MAX},busy=0,read_count=0;
 bool irq=false,primed=false,loaded=false;uint8_t latch=0;
 z80 cpu{};Iface iface;ymfm::ym2151 fm;
 std::vector<uint8_t> program,pcm;
 std::array<uint8_t,2048> ram{};std::array<uint8_t,256> unused{};
 std::array<Voice,16> voice{};std::deque<Event> events;
 float pcm_left=0,pcm_right=0,previous[2]{},next[2]{};double fraction=0;
 Trace tracer=nullptr;void*trace_user=nullptr;
 Impl():iface(*this),fm(iface){}
 void log(const char*kind,unsigned addr,unsigned value){if(tracer)tracer(trace_user,clock,kind,addr,value);}
 static uint8_t memory_read(void*ctx,uint16_t a){
  auto&h=*static_cast<Impl*>(ctx);
  if(a<0xf000)return h.program[a];
  if(a>=0xf800)return h.ram[a&2047];
  unsigned port=a&255,n=(port&0x78)>>3,k=port&0x87;auto&v=h.voice[n];
  switch(k){case 2:return v.left;case 3:return v.right;case 4:return uint8_t(v.repeat);case 5:return uint8_t(v.repeat>>8);case 6:return v.page;case 7:return v.step;case 0x84:return uint8_t(v.position>>8);case 0x85:return uint8_t(v.position>>16);case 0x86:return v.flags;default:return h.unused[port];}
 }
 static void memory_write(void*ctx,uint16_t a,uint8_t value){
  auto&h=*static_cast<Impl*>(ctx);
  if(a>=0xf800){h.ram[a&2047]=value;return;}
  if(a<0xf000)return;
  unsigned port=a&255,n=(port&0x78)>>3,k=port&0x87;auto&v=h.voice[n];h.log("pcm",port,value);
  switch(k){case 2:v.left=value;break;case 3:v.right=value;break;case 4:v.repeat=(v.repeat&0xff00)|value;break;case 5:v.repeat=(v.repeat&255)|(uint16_t(value)<<8);break;case 6:v.page=value;break;case 7:v.step=value;break;case 0x84:v.position=(v.position&0xff00ff)|(uint32_t(value)<<8);break;case 0x85:v.position=(v.position&0x00ffff)|(uint32_t(value)<<16);break;case 0x86:v.flags=value;break;default:h.unused[port]=value;}
 }
 static uint8_t input(z80*z,uint8_t port){
  auto&h=*static_cast<Impl*>(z->userdata);
  if((port&0xc0)==0x40){++h.read_count;h.log("command",port,h.latch);return h.latch;}
  return (port&0xc0)==0?h.fm.read_status():255;
 }
 static void output(z80*z,uint8_t port,uint8_t value){auto&h=*static_cast<Impl*>(z->userdata);if((port&0xc0)==0){h.log("ym",port&1,value);h.fm.write(port&1,value);}}
 void reset(){
  clock=0;next_sample=64;timer[0]=timer[1]=UINT64_MAX;busy=0;irq=false;latch=0;read_count=0;
  ram.fill(0);unused.fill(255);voice={};events.clear();fm.reset();z80_init(&cpu);
  cpu.userdata=this;cpu.read_byte=memory_read;cpu.write_byte=memory_write;cpu.port_in=input;cpu.port_out=output;
  pcm_left=pcm_right=previous[0]=previous[1]=next[0]=next[1]=0;fraction=0;primed=false;
 }
 void execute(uint64_t until){
  while(clock<until){
   while(!events.empty()&&events.front().cycle<=clock){latch=events.front().value;events.pop_front();z80_gen_nmi(&cpu);}
   for(unsigned n=0;n<2;n++)if(timer[n]<=clock){timer[n]=UINT64_MAX;iface.expire(n);}
   if(irq)z80_gen_int(&cpu,255);
   uint32_t before=uint32_t(cpu.cyc);z80_step(&cpu);clock+=uint32_t(uint32_t(cpu.cyc)-before);
  }
 }
 void pcm_tick(){
  int left=0,right=0;
  for(auto&v:voice){
   if(v.flags&1){v.position&=0xffff00;continue;}
   if(uint8_t(v.position>>16)==v.page){
    if(v.flags&2){v.flags|=1;continue;}
    v.position=uint32_t(v.repeat)<<8;
   }
   unsigned address=((v.flags&0x70)<<12)+(v.position>>8);int value=int(pcm[address&0x7ffff])-128;
   left+=value*(v.left&127);right+=value*(v.right&127);v.position=(v.position+v.step)&0xffffff;
  }
  pcm_left=float(left)*(.35f/32768);pcm_right=float(right)*(.35f/32768);
 }
 void sample(float*out){
  execute(next_sample);ymfm::ym2151::output_data y;fm.generate(&y,1);
  if(!(next_sample&127))pcm_tick();
  out[0]=float(y.data[0])*(.15f/32768)+pcm_left;out[1]=float(y.data[1])*(.15f/32768)+pcm_right;next_sample+=64;
 }
};
SmgpArcadeSound::SmgpArcadeSound():p(new Impl){}
SmgpArcadeSound::~SmgpArcadeSound()=default;
bool SmgpArcadeSound::load(const std::string&file){
 std::ifstream f(file,std::ios::binary);char magic[4]{};uint32_t size=0,bank=0;f.read(magic,4);f.read(reinterpret_cast<char*>(&size),4);f.read(reinterpret_cast<char*>(&bank),4);
 if(!f||std::memcmp(magic,"SB17",4)||size!=65536||bank!=524288)return false;
 p->program.resize(size);p->pcm.resize(bank);f.read(reinterpret_cast<char*>(p->program.data()),size);f.read(reinterpret_cast<char*>(p->pcm.data()),bank);
 if(!f)return false;p->loaded=true;p->reset();return true;
}
void SmgpArcadeSound::reset(){if(p->loaded)p->reset();}
void SmgpArcadeSound::packet(const std::array<uint8_t,11>&data){uint64_t at=p->clock+64;for(unsigned i=0;i<data.size();i++){p->events.push_back({at,data[i]});at+=i==0?280:160;}}
void SmgpArcadeSound::command_at(double seconds,uint8_t value){p->events.push_back({uint64_t(std::llround(seconds*4000000)),value});}
void SmgpArcadeSound::trace(Trace fn,void*user){p->tracer=fn;p->trace_user=user;}
uint64_t SmgpArcadeSound::commands_read()const{return p->read_count;}
void SmgpArcadeSound::render(float*out,size_t frames,double rate){
 if(!p->loaded||rate<1){std::fill_n(out,frames*2,0.f);return;}
 if(!p->primed){p->sample(p->next);p->primed=true;}
 const double step=62500/rate;
 for(size_t n=0;n<frames;n++){
  out[n*2]=float(p->previous[0]+(p->next[0]-p->previous[0])*p->fraction);out[n*2+1]=float(p->previous[1]+(p->next[1]-p->previous[1])*p->fraction);
  p->fraction+=step;
  while(p->fraction>=1){p->previous[0]=p->next[0];p->previous[1]=p->next[1];p->sample(p->next);p->fraction-=1;}
 }
}
