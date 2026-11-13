#pragma once
#include <windows.h>
#include <filesystem>
void smgp_open_control_editor(HWND owner,const std::filesystem::path &config);
bool smgp_control_editor_message(MSG *message);
int smgp_control_editor_qa(HWND owner,const std::filesystem::path &config,const std::filesystem::path &capture);
