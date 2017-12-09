@echo off
set "VSCMD_START_DIR=%CD%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\Common7\Tools\VsDevCmd.bat"

set tools_dir=..\..\..\..\tools
set bin2hex=%tools_dir%\bin2hex.exe
set bin2hex_cpp=%tools_dir%\bin2hex.cpp

if not exist %bin2hex% (
    cl.exe /EHsc /nologo /Fe%tools_dir%\ /Fo%tools_dir%\ %bin2hex_cpp%
)

set PATH=%tools_dir%;%PATH%

fxc.exe /T vs_4_0 /E single_texture_vs /Fo shader.bin shaders.hlsl
%bin2hex% shader.bin single_texture_vs > hlsl_compiled/single_texture_vs.cpp
del shader.bin

fxc.exe /T vs_4_0 /E multi_texture_vs /Fo shader.bin shaders.hlsl
%bin2hex% shader.bin multi_texture_vs > hlsl_compiled/multi_texture_vs.cpp
del shader.bin

fxc.exe /T ps_4_0 /E single_texture_ps /Fo shader.bin shaders.hlsl
%bin2hex% shader.bin single_texture_ps > hlsl_compiled/single_texture_ps.cpp
del shader.bin

fxc.exe /T ps_4_0 /E multi_texture_mul_ps /Fo shader.bin shaders.hlsl
%bin2hex% shader.bin multi_texture_mul_ps > hlsl_compiled/multi_texture_mul_ps.cpp
del shader.bin

fxc.exe /T ps_4_0 /E multi_texture_add_ps /Fo shader.bin shaders.hlsl
%bin2hex% shader.bin multi_texture_add_ps > hlsl_compiled/multi_texture_add_ps.cpp
del shader.bin
