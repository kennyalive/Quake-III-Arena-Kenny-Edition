# Welcome to Quake 3 source code!

![Actions Status](https://github.com/kennyalive/Quake-III-Arena-Kenny-Edition/actions/workflows/msbuild.yml/badge.svg)

## What's in this repository
* This repository contains Quake 3 Arena source code which can be built with modern versions of Visual Studio.
* Only Windows x64 platform is supported.
* I don't try to fix bugs inherited from the original Q3 source code distribution - in this regard the project is Q3-bugs-friendly. I still make fixes to functionality that did not stand the test of time (SetDeviceGammaRamp).
* Some functionality related to ancient graphics hardware was removed. Also I removed compilation of qvm code to native instructions to simplify maintenance. This means all game code is run through QVM which is slower than native execution but fast enough for modern computers.
* No changes to visuals or gameplay. Vulkan backend is now enabled by default. This change was made due to issues with the SetDeviceGammaRamp API.

## Usage
* Build `visual-studio/quake3.sln` solution and copy `quake3-ke.exe` to your local Quake-III-Arena installation folder.
* To debug the game from Visual Studio, go to `quake3` project settings -> Debugging -> Command Arguments. Specify the game installation location: `+set fs_basepath <quake3/installation/directory>`

## Vulkan support 
The Vulkan backend supports everything provided by the original OpenGL version, including all available `r_` cvars. No new features have been added; the goal is to preserve existing functionality rather than expand it.

#### New cvars:
* **r_renderAPI** - 3D API to use. Requires vid_restart.
    * 0 - OpenGL
    * 1 - Vulkan
    * 2 - DirectX 12 (if compiled with `ENABLE_DX12`)
    * 3 - WebGPU placeholder (blank window; rendering and screenshots are not implemented)
 
* **r_gpu** - Select GPU in multi-GPU system (zero-based index). By default, GPU 0 is selected. Requires vid_restart.

* **r_vsync** - Enable vsync in Vulkan. Requires vid_restart.

* **r_shaderGamma** - Use compute shader to apply gamma instead of legacy HW gamma API (SetDeviceGammaRamp). 

* **r_twinMode** - Debug feature to compare rendering output between OpenGL/Vulkan/DX12 APIs, with a WebGPU placeholder window available. Requires vid_restart.

* **r_twinAPIs** - Select additional API windows when `r_twinMode` is enabled. Requires vid_restart. Add these bitmask values:
    * 1 - OpenGL
    * 2 - Vulkan
    * 4 - DirectX 12 (if compiled with `ENABLE_DX12`)
    * 8 - WebGPU placeholder (blank window)
    * 15 - All available APIs, including the WebGPU placeholder (default)
    * 0 - Main window only
    * The main API selected by `r_renderAPI` always stays active; its bit in the mask is ignored. This setting has no effect when `r_twinMode` is disabled.
    * Example: `\r_renderAPI 1`, `\r_twinMode 1`, `\r_twinAPIs 4`, then `\vid_restart` enables Vulkan + DX12 without OpenGL.
    * WebGPU example: `\r_renderAPI 1`, `\r_twinMode 1`, `\r_twinAPIs 8`, then `\vid_restart` opens Vulkan with a blank WebGPU twin window.

![twin_mode](https://user-images.githubusercontent.com/4964024/34961607-48aae882-fa40-11e7-9bf0-d4400afdad34.jpg)

#### Additional information:
* Q: How to start game with vulkan support? A: `quake3-ke.exe +set r_renderAPI 1`.
* Q: How to enable vulkan support from Q3 console? A: `\r_renderAPI 1` then `\vid_restart`.
* Q: How to enable twin mode from Q3 console? A: `\r_twinMode 1` or `\r_twinMode 1` then `\vid_restart`.
* Q: How to check that Vulkan backend is really active? A: `gfxinfo` console command reports information about active rendering backend.

![quake3-ke](https://user-images.githubusercontent.com/4964024/28160268-4f0707d4-67c8-11e7-9009-8540789aab0b.jpeg)
