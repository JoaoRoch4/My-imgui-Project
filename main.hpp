#include<iostream>
#include <Windows.h>
#include "pch.hpp"            // cabeçalho pré-compilado — SEMPRE PRIMEIRO
#include "App.hpp"            // classe App + extern App* g_App
#include "WindowsConsole.hpp" // console externo Win32 com hotkey

int main(int argc, char *argv[]);

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     LPWSTR    lpCmdLine,
    _In_     int       nCmdShow);
