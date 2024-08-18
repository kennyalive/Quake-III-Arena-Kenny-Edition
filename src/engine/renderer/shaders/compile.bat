@echo off
set "VSCMD_START_DIR=%CD%"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"

set tools_dir=..\..\..\..\tools
set bin2hex=%tools_dir%\bin2hex.exe
set bin2hex_cpp=%tools_dir%\bin2hex.cpp

if not exist %bin2hex% (
    cl.exe /EHsc /nologo /Fe%tools_dir%\ /Fo%tools_dir%\ %bin2hex_cpp%
)

set PATH=%tools_dir%;%PATH%

for %%f in (*.vert) do (
    %VULKAN_SDK%\Bin\glslangValidator.exe -V %%f
    %bin2hex% vert.spv %%~nf_vert_spv > spirv/%%~nf_vert.cpp
    del vert.spv
)

for %%f in (*.frag) do (
    %VULKAN_SDK%\Bin\glslangValidator.exe -V %%f
    %bin2hex% frag.spv %%~nf_frag_spv > spirv/%%~nf_frag.cpp
    del frag.spv
)

for %%f in (*.comp) do (
    %VULKAN_SDK%\Bin\glslangValidator.exe -V %%f
    %bin2hex% comp.spv %%~nf_comp_spv > spirv/%%~nf_comp.cpp
    del comp.spv
)
