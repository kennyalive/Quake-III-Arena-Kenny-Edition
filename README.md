# Welcome to Quake 3 source code!

![Actions Status](https://github.com/kennyalive/Quake-III-Arena-Kenny-Edition/actions/workflows/msbuild.yml/badge.svg)

## What's in this repository
* This repository contains Quake 3 Arena source code which can be built with modern versions of Visual Studio.
* Only Windows x64 platform is supported.
* I don't try to fix bugs inherited from the original Q3 source code distribution - in this regard the project is Q3-bugs-friendly. I still make fixes if the compiler complains about something or if functionality starts to shows its age (SetDeviceGammaRamp).
* Some functionality related to ancient graphics hardware was removed. Also I removed compilation of qvm code to native instructions to simplify maintenance. This means all game code is run through QVM which is slower than native execution but fast enough for modern computers.
* No changes in terms of visual appearence or gameplay. Provided Vulkan backend is enabled by default. It produces the same pixels as OpenGL backend. The decision to make Vulkan a default backend was made after the issues with SetDeviceGammaRamp API.

## Usage
* Build `visual-studio/quake3.sln` solution and copy `quake3-ke.exe` to your local Quake-III-Arena installation folder.
* To debug the game from Visual Studio go to quake3 project settings -> Debugging -> Command Arguments. Specify game installation location: `+set fs_basepath <quake3/installation/directory>`

## Vulkan support 
Vulkan backend provides the same graphics features as the original OpenGL-based one, including support of all renderer `r_` cvars (except the few ones that were removed as part of this project maintenance). For example, cvars for debug visualization are supported. Prototype implementation of stencil shadows is supported in Vulkan backend too (yes, Q3A shipped it, disabled by default). What does Vulkan backend add that does not exist in OpenGL version? It adds nothing new. The purpose of this project is not to create extra but to preserve what already exists.

#### New cvars:
* **r_renderAPI** - 3D API to use. Requires vid_restart.
    * 0 - OpenGL
    * 1 - Vulkan
 
* **r_gpu** - Selects GPU in multi-GPU system (zero-based index). By default, GPU 0 is selected. Requires vid_restart.

* **r_vsync** - Enables vsync in Vulkan. It is recommended to set com_maxFPS to 0 if the monitor's refresh rate is higher than the current com_maxFPS value. Requires vid_restart.

* **r_shaderGamma** - Use compute shader to apply gamma instead of legacy HW gamma API (SetDeviceGammaRamp). 

* **r_twinMode** - Debug feature to compare rendering output between OpenGL/Vulkan APIs. Requires vid_restart.

![twin_mode](https://user-images.githubusercontent.com/4964024/34961607-48aae882-fa40-11e7-9bf0-d4400afdad34.jpg)

#### Additional information:
* Q: How to start game with vulkan support? A: `quake3-ke.exe +set r_renderAPI 1`.
* Q: How to enable vulkan support from Q3 console? A: `\r_renderAPI 1` then `\vid_restart`.
* Q: How to enable twin mode from Q3 console? A: `\r_twinMode 1` or `\r_twinMode 1` then `\vid_restart`.
* Q: How to check that Vulkan backend is really active? A: `gfxinfo` console command reports information about active rendering backend.

![quake3-ke](https://user-images.githubusercontent.com/4964024/28160268-4f0707d4-67c8-11e7-9009-8540789aab0b.jpeg)
