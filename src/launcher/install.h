#pragma once
#include <windows.h>
#include <filesystem>
void smgp_install_init(HINSTANCE instance,HFONT font,const std::filesystem::path& package);
void smgp_install_show(HWND owner);
bool smgp_install_message(MSG& message);
bool smgp_install_busy();
HWND smgp_install_window();
