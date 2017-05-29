# Welcome to the Quake 3 source code!

This repository contains updated version of the original Q3 codebase with reorganized code structure, compatibility fixes, build setup for the latest Visual Studio and modifications that update the core tech but preserve original gameplay, look and feel.

The general direction is to simplify the things. The codebase should be easy to build, executable does not require additional shared libraries to work and we do not talk about OOP here. Only Windows platform is supported.

## Vulkan support
The codebase supports Vulkan API. There are no difference in graphics between OpenGL and Vulkan modes.
In addition to base rendering functionality and graphics customization options Vulkan backend also supports Q3 debug visualization features.

Make sure that the latest AMD/NVIDIA drivers are installed. Vulkan backend was tested on AMD and NVIDIA hardware. Unfortunately no guarantees for Intel GPUs - there is no vulkan support for my 4790K Haswell processor.

By default OpenGL backend is active. To enable Vulkan backend start game with:
    `quake3-ke.exe +set r_renderAPI 1`
    

#### New cvars:
* **r_renderAPI** - 3D API to use: 0 - OpenGL, 1 - Vulkan. Requires vid_restart.

* **r_twinMode** - Debug feature to compare rendering output between OpenGL and Vulkan APIs.
    If enabled, renderer uses both APIs and renders current frame to two side-by-side windows.
    Requires vid_restart.
    
Q: How to check that Vulkan backend is really active?

A: _gfxinfo_ console command reports information about active rendering backend.

    
## Visual Studio
The project files are provides for Visual Studio 2017. Free community version is available online.

![Screenshot](https://github.com/artemalive/Quake-III-Arena/raw/master/Screenshot.jpg)
