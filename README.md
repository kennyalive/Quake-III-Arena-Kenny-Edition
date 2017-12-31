# Welcome to the Quake 3 source code!

<img src="https://ci.appveyor.com/api/projects/status/github/kennyalive/Quake-III-Arena-Kenny-Edition?svg=true" alt="Project Badge">

This repository contains updated version of the original Q3 codebase with reorganized code structure, compatibility fixes, build setup for the latest Visual Studio and modifications that update the core tech but **preserve original gameplay, look and feel**.

## Usage
* Build `visual-studio/quake3.sln` solution.
* Copy `quake3-ke.exe` to your local Quake-III-Arena installation folder.

## Vulkan and DX12 support 
Vulkan backend provides the same graphics features as original OpenGL-based one including customization via r_xxx cvars and debug visualization.

Initially DirectX 12 backend was an exercise to learn this graphics API. It turned out the implementation is quite concise, so it was merged into the main branch. There is an option to compile the project without DX12 backend. For this, uncomment DISABLE_DX12 macro in dx.h header.

#### New cvars:
* **r_renderAPI** - 3D API to use: 0 - OpenGL, 1 - Vulkan, 2 - DX12. Requires vid_restart.

* **r_twinMode** - Debug feature to compare rendering output between OpenGL/Vulkan/DX12 APIs. Requires vid_restart.
  * r_twinMode=1 : one additional window is created. If the main window uses graphics API defined by r_renderAPI then the additional window will use graphics API with index (r_renderAPI+1)%3
  * r_twinMode=2 : two additional windows are created and all 3 graphics APIs are active simultaneously.

#### Additional information:
* Q: How to start game with vulkan support? A: `quake3-ke.exe +set r_renderAPI 1`.
* Q: How to enable vulkan support from Q3 console? A: `\r_renderAPI 1` then `\vid_restart`.
* Q: How to enable twin mode from Q3 console? A: `\r_twinMode 7` then `\vid_restart`.
* Q: How to check that Vulkan backend is really active? A: `gfxinfo` console command reports information about active rendering backend.

## Visual Studio
The project files are provided for Visual Studio 2017. Free community version is available online.

`visual-studio/quake3.vcxproj.user.example` file is provided with example configuration options to start the game from visual studio and to enable vulkan validation layers.

![quake3-ke](https://user-images.githubusercontent.com/4964024/28160268-4f0707d4-67c8-11e7-9009-8540789aab0b.jpeg)
