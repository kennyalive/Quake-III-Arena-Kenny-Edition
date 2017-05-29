* Only windows platform is supported.
* Only 64-bit compilation is supported.
* The codebase is compiled as C++ code.
* Project files are provided for Visual Studio 2017.
* Vulkan support.

New cvars:
* r_renderAPI - 3D API to use: 0 - OpenGL, 1 - Vulkan. Requires vid_restart.

* r_twinMode - Debug feature to compare rendering output between OpenGL and Vulkan APIs.
    If enabled, renderer uses both APIs and renders current frame to two side-by-side windows.
    Requires vid_restart.

Removed cvars:
* r_allowExtensions (always use extensions if available)
* r_allowSoftwareGL
* r_colorbits (use desktop color depth)
* r_displayRefresh
* r_dlightBacks
* r_ext_multitexture (required)
* r_finish
* r_ignore
* r_ignoreFastPath
* r_maskMinidriver
* r_measureOverdraw
* r_primitives (always use qglDrawElements)
