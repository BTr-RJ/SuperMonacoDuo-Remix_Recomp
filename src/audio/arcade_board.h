#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
class SmgpArcadeSound {
public:
 using Trace=void(*)(void*,uint64_t,const char*,unsigned,unsigned);
 SmgpArcadeSound();~SmgpArcadeSound();
 bool load(const std::string&file);
 void reset();
 void packet(const std::array<uint8_t,11>&data);
 void command_at(double seconds,uint8_t data);
 void render(float*stereo,size_t frames,double rate);
 void trace(Trace fn,void*user);
 uint64_t commands_read()const;
private:
 struct Impl;std::unique_ptr<Impl> p;
};
