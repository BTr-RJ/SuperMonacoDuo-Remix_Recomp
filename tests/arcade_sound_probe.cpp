#include "arcade_board.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
static void trace(void*user,uint64_t cycle,const char*kind,unsigned a,unsigned v){fprintf(static_cast<FILE*>(user),"%llu,%s,%02X,%02X\n",(unsigned long long)cycle,kind,a,v);}
static void word(FILE*f,unsigned value){fputc(value&255,f);fputc((value>>8)&255,f);}
static void dword(FILE*f,unsigned value){word(f,value);word(f,value>>16);}
int main(int argc,char**argv){
 if(argc<5)return 2;SmgpArcadeSound board;if(!board.load(argv[1]))return 3;
 double seconds=std::atof(argv[3]);std::string mode=argv[4];FILE*log=argc>5?fopen(argv[5],"w"):nullptr;if(log)board.trace(trace,log);
 if(mode=="replay"){
  std::ifstream input(argv[6]);std::string line;std::getline(input,line);
  while(std::getline(input,line)){std::stringstream s(line);std::string frame,time,dev,kind,addr,value;std::getline(s,frame,',');std::getline(s,time,',');std::getline(s,dev,',');std::getline(s,kind,',');std::getline(s,addr,',');std::getline(s,value,',');if(dev==":mainpcb:soundcpu"&&kind=="command")board.command_at(std::atof(time.c_str()),uint8_t(std::strtoul(value.c_str(),nullptr,16)));}
 }
 FILE*f=fopen(argv[2],"wb");if(!f)return 4;unsigned frames=unsigned(seconds*48000);fwrite("RIFF",1,4,f);dword(f,36+frames*4);fwrite("WAVEfmt ",1,8,f);dword(f,16);word(f,1);word(f,2);dword(f,48000);dword(f,192000);word(f,4);word(f,16);fwrite("data",1,4,f);dword(f,frames*4);
 float data[1600];unsigned written=0;
 while(written<frames){unsigned count=std::min(800u,frames-written);
  if(mode!="replay"){
   std::array<uint8_t,11> p{0x80,0,0,4,127,0,0,0,0,0,0};
   if(mode=="player")p[2]=uint8_t(4+std::min(110u,written/5000));
   if(mode.rfind("traffic",0)==0){
    unsigned distance=unsigned(std::abs(int(written)-144000)*110/144000);
    p[5]=uint8_t(distance|(written>144000?128:0));p[6]=8;
    if(mode=="traffic-left"){p[5]=36;p[6]=1;}
    if(mode=="traffic-right"){p[5]=36;p[6]=15;}
    if(mode=="traffic-near")p[5]=8;
    if(mode=="traffic-far")p[5]=100;
   }
   if(mode=="skid")p[0]=((written>=48000&&written<144000)||(written>=192000&&written<240000))?0x08:0x00;
   if(mode=="impact"&&(written==48000||written==192000))p[0]=0xa7;
   if(mode.rfind("cmd-",0)==0&&written==48000)p[0]=uint8_t(std::strtoul(mode.c_str()+4,nullptr,16));
   board.packet(p);
  }
  board.render(data,count,48000);for(unsigned i=0;i<count*2;i++){int value=int(data[i]*32768);value=std::max(-32768,std::min(32767,value));word(f,unsigned(uint16_t(value)));}written+=count;
 }
 fclose(f);if(log)fclose(log);printf("Read %llu original sound commands\n",(unsigned long long)board.commands_read());return 0;
}
