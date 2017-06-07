# Welcome to the Quake 3 source code!

This repository contains updated version of the original Q3 codebase with reorganized code structure, compatibility fixes, build setup for the latest Visual Studio and modifications that update the core tech but preserve original gameplay, look and feel.

The general direction is to simplify the things. The codebase should be easy to build, executable does not require additional shared libraries to work and we do not talk about OOP here. Only Windows platform is supported.

## Usage
* Build `visual-studio/quake3.sln` solution.
* Copy `quake3-ke.exe` to your local Quake-III-Arena installation folder.

## Vulkan support
Vulkan backend provides the same graphics features as original OpenGL-based one including customization via r_xxx cvars and debug visualization.

Make sure that the latest AMD/NVIDIA drivers are installed. Vulkan backend was tested on AMD and NVIDIA hardware. Unfortunately no guarantees for Intel GPUs - there is no vulkan support for my 4790K Haswell processor.

#### New cvars:
* **r_renderAPI** - 3D API to use: 0 - OpenGL, 1 - Vulkan. Requires vid_restart.

* **r_twinMode** - Debug feature to compare rendering output between OpenGL and Vulkan APIs.
    If enabled, renderer uses both APIs and renders current frame to two side-by-side windows.
    Requires vid_restart.
    
#### Additional information:
* Q: How to start game with vulkan support? A: `quake3-ke.exe +set r_renderAPI 1`.
* Q: How to enable vulkan support from Q3 console? A: `\r_renderAPI 1` then `\vid_restart`.
* Q: How to enable twin mode from Q3 console? A: `\r_twinMode 1` then `\vid_restart`.
* Q: How to check that Vulkan backend is really active? A: `gfxinfo` console command reports information about active rendering backend.

## Visual Studio
The project files are provides for Visual Studio 2017. Free community version is available online.

`visual-studio/quake3.vcxproj.user.example` file is provided with example configuration options to start game from visual studio and to enable vulkan validation layers.

![Screenshot](https://github.com/artemalive/Quake-III-Arena/raw/master/Screenshot.jpg)
</br>
P.S. The aesthetically pleasing screenshot above is a historical artifact that was created somewhere in 2005 and has no direct relation to the codebase itself.
