#pragma once
#include <windows.h>
#include <filesystem>
void smgp_updates_init(HINSTANCE app,HFONT font,const std::filesystem::path& root,bool qa,bool (*ready)());
void smgp_updates_startup(HWND owner);
void smgp_updates_show(HWND owner);
bool smgp_updates_event(HWND window,UINT message,WPARAM wp,LPARAM lp);
bool smgp_updates_message(MSG& message);
HWND smgp_updates_window();
void smgp_updates_shutdown();
HANDLE smgp_updates_mutex(const std::filesystem::path& root,bool own);
int smgp_updates_host(HINSTANCE app,int argc,wchar_t **argv);
bool smgp_updates_recovery(const std::filesystem::path& root);
