/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
/*
** WIN_GLIMP.C
**
** This file contains ALL Win32 specific stuff having to do with the
** OpenGL/Vulkan refresh.  When a port is being made the following functions
** must be implemented by the port:
**
** GLimp_EndFrame
** GLimp_Init
** GLimp_LogComment
** GLimp_Shutdown
**
** vk_imp_init
** vk_imp_shutdown
** vk_imp_create_surface
*/
#include "../renderer/tr_local.h"
#include "resource.h"
#include "win_local.h"

#define	MAIN_WINDOW_CLASS_NAME	"Quake 3: Arena"
#define	TWIN_WINDOW_CLASS_NAME	"Quake 3: Arena [Twin]"

static bool s_main_window_class_registered = false;
static bool s_twin_window_class_registered = false;

static HDC gl_hdc; // handle to device context
static HGLRC gl_hglrc; // handle to GL rendering context

static HINSTANCE vk_library_handle; // HINSTANCE for the Vulkan library

FILE* log_fp;

qboolean QGL_Init(const char *dllname);
void QGL_Shutdown();
void QGL_EnableLogging(qboolean enable);
void WG_CheckHardwareGamma();
void WG_RestoreGamma();

static int GetDesktopCaps(int index) {
    HDC hdc = GetDC(GetDesktopWindow());
    int value = GetDeviceCaps(hdc, index);
    ReleaseDC(GetDesktopWindow(), hdc);
    return value;
}
static int GetDesktopColorDepth() { return GetDesktopCaps(BITSPIXEL); }
static int GetDesktopWidth() { return GetDesktopCaps(HORZRES); }
static int GetDesktopHeight() { return GetDesktopCaps(VERTRES); }

/*
** ChoosePFD
**
** Helper function that replaces ChoosePixelFormat.
*/
static int GLW_ChoosePixelFormat(HDC hDC, const PIXELFORMATDESCRIPTOR *pPFD)
{
    const int MAX_PFDS = 512;
	PIXELFORMATDESCRIPTOR pfds[MAX_PFDS+1];

	ri.Printf(PRINT_ALL, "...GLW_ChoosePFD( %d, %d, %d )\n", (int) pPFD->cColorBits, (int) pPFD->cDepthBits, (int) pPFD->cStencilBits);

	// count number of PFDs
	int maxPFD = DescribePixelFormat(hDC, 1, sizeof( PIXELFORMATDESCRIPTOR ), &pfds[0]);
	if (maxPFD > MAX_PFDS) {
		ri.Printf(PRINT_WARNING, "...numPFDs > MAX_PFDS (%d > %d)\n", maxPFD, MAX_PFDS);
		maxPFD = MAX_PFDS;
	}

	ri.Printf(PRINT_ALL, "...%d PFDs found\n", maxPFD);

	// grab information
	for (int i = 1; i <= maxPFD; i++) {
		DescribePixelFormat(hDC, i, sizeof(PIXELFORMATDESCRIPTOR), &pfds[i]);
	}

	// look for a best match
    int bestMatch = 0;
	for (int i = 1; i <= maxPFD; i++ ) {
        if (bestMatch != 0 &&
            (pfds[bestMatch].dwFlags & PFD_STEREO) == (pPFD->dwFlags & PFD_STEREO) &&
            pfds[bestMatch].cColorBits == pPFD->cColorBits &&
            pfds[bestMatch].cDepthBits == pPFD->cDepthBits &&
            pfds[bestMatch].cStencilBits == pPFD->cStencilBits)
        {
            break;
        }

		//
		// make sure this has hardware acceleration
		//
		if ((pfds[i].dwFlags & PFD_GENERIC_FORMAT) != 0)  {
			if (r_verbose->integer) {
				ri.Printf( PRINT_ALL, "...PFD %d rejected, software acceleration\n", i );
            }
            continue;
		}

		// verify pixel type
		if (pfds[i].iPixelType != PFD_TYPE_RGBA) {
			if (r_verbose->integer) {
				ri.Printf( PRINT_ALL, "...PFD %d rejected, not RGBA\n", i );
			}
			continue;
		}

		// verify proper flags
		if ((pfds[i].dwFlags & pPFD->dwFlags) != pPFD->dwFlags) {
			if (r_verbose->integer) {
				ri.Printf( PRINT_ALL, "...PFD %d rejected, improper flags (%x instead of %x)\n", i, pfds[i].dwFlags, pPFD->dwFlags );
			}
			continue;
		}

		// verify enough bits
		if (pfds[i].cDepthBits < 15) {
			continue;
		}
		if ((pfds[i].cStencilBits < 4) && (pPFD->cStencilBits > 0)) {
			continue;
		}

        if (!bestMatch) {
            bestMatch = i;
            continue;
        }

		//
		// selection criteria (in order of priority):
		// 
		//  PFD_STEREO
		//  colorBits
		//  depthBits
		//  stencilBits
		//
        bool same_stereo = (pfds[i].dwFlags & PFD_STEREO) == (pfds[bestMatch].dwFlags & PFD_STEREO);
        bool better_stereo = (pfds[i].dwFlags & PFD_STEREO) == (pPFD->dwFlags & PFD_STEREO) &&
            (pfds[bestMatch].dwFlags & PFD_STEREO) != (pPFD->dwFlags & PFD_STEREO);

        bool same_color = pfds[i].cColorBits == pfds[bestMatch].cColorBits;
        bool better_color = (pfds[bestMatch].cColorBits >= pPFD->cColorBits)
            ? pfds[i].cColorBits >= pPFD->cColorBits && pfds[i].cColorBits < pfds[bestMatch].cColorBits
            : pfds[i].cColorBits > pfds[bestMatch].cColorBits;

        bool same_depth = pfds[i].cDepthBits == pfds[bestMatch].cDepthBits;
        bool better_depth = (pfds[bestMatch].cDepthBits >= pPFD->cDepthBits)
            ? pfds[i].cDepthBits >= pPFD->cDepthBits && pfds[i].cDepthBits < pfds[bestMatch].cDepthBits
            : pfds[i].cDepthBits > pfds[bestMatch].cDepthBits;

        bool better_stencil;
        if (pPFD->cStencilBits == 0)
            better_stencil = pfds[i].cStencilBits == 0 && pfds[bestMatch].cStencilBits != 0;
        else
            better_stencil = (pfds[bestMatch].cStencilBits >= pPFD->cStencilBits)
                ? pfds[i].cStencilBits >= pPFD->cStencilBits && pfds[i].cStencilBits < pfds[bestMatch].cStencilBits
                : pfds[i].cStencilBits > pfds[bestMatch].cStencilBits;

        if (better_stereo)
            bestMatch = i;
        else if (same_stereo) {
            if (better_color)
                bestMatch = i;
            else if (same_color) {
                if (better_depth)
                    bestMatch = i;
                else if (same_depth) {
                    if (better_stencil)
                        bestMatch = i;
                }
            }
        }
	}

	if ( !bestMatch )
		return 0;

	if ((pfds[bestMatch].dwFlags & PFD_GENERIC_FORMAT) != 0 || (pfds[bestMatch].dwFlags & PFD_GENERIC_ACCELERATED) != 0) {
        ri.Printf(PRINT_ALL, "...no hardware acceleration found\n");
        return 0;
	}

    ri.Printf(PRINT_ALL, "...hardware acceleration found\n");
	return bestMatch;
}

static bool GLW_SetPixelFormat(HDC hdc, PIXELFORMATDESCRIPTOR *pPFD, int colorbits, int depthbits, int stencilbits, qboolean stereo) {
    const PIXELFORMATDESCRIPTOR pfd_base =
    {
        sizeof(PIXELFORMATDESCRIPTOR),	// size of this pfd
        1,								// version number
        PFD_DRAW_TO_WINDOW |			// support window
        PFD_SUPPORT_OPENGL |			// support OpenGL
        PFD_DOUBLEBUFFER,				// double buffered
        PFD_TYPE_RGBA,					// RGBA type
        0,	                			// color bits
        0, 0, 0, 0, 0, 0,				// color bits ignored
        0,								// no alpha buffer
        0,								// shift bit ignored
        0,								// no accumulation buffer
        0, 0, 0, 0, 					// accum bits ignored
        0,	                			// z-buffer	bits
        0,              	    		// stencil buffer bits
        0,								// no auxiliary buffer
        PFD_MAIN_PLANE,					// main layer
        0,								// reserved
        0, 0, 0							// layer masks ignored
    };

    *pPFD = pfd_base;

    pPFD->cColorBits = (BYTE)colorbits;
    pPFD->cDepthBits = (BYTE)depthbits;
    pPFD->cStencilBits = (BYTE)stencilbits;

    if (stereo)
    {
        ri.Printf(PRINT_ALL, "...attempting to use stereo\n");
        pPFD->dwFlags |= PFD_STEREO;
    }

    int pixelformat = GLW_ChoosePixelFormat(hdc, pPFD);
	if (pixelformat == 0) {
		ri.Printf( PRINT_ALL, "...GLW_ChoosePixelFormat failed\n");
		return false;
	}
	ri.Printf( PRINT_ALL, "...PIXELFORMAT %d selected\n", pixelformat );

	DescribePixelFormat(hdc, pixelformat, sizeof( *pPFD ), pPFD);
	if (SetPixelFormat(hdc, pixelformat, pPFD ) == FALSE) {
		ri.Printf (PRINT_ALL, "...SetPixelFormat failed\n", hdc);
		return false;
	}
    return true;
}

// Sets pixel format and creates opengl context for the given window.
static qboolean GLW_InitDriver(HWND hwnd) {
	ri.Printf( PRINT_ALL, "Initializing OpenGL driver\n" );

	//
	// get a DC for our window
	//
	ri.Printf(PRINT_ALL, "...getting DC: ");
    gl_hdc = GetDC(hwnd);
	if (gl_hdc == NULL) {
		ri.Printf(PRINT_ALL, "failed\n");
		return qfalse;
	}
	ri.Printf(PRINT_ALL, "succeeded\n");

	//
	// set pixel format
	//
    int colorbits = GetDesktopColorDepth();
    int depthbits = (r_depthbits->integer == 0) ? 24 : r_depthbits->integer;
    int stencilbits = r_stencilbits->integer;

    PIXELFORMATDESCRIPTOR pfd;
    if (!GLW_SetPixelFormat(gl_hdc, &pfd, colorbits, depthbits, stencilbits, (qboolean)r_stereo->integer)) {
        ReleaseDC(hwnd, gl_hdc);
		gl_hdc = NULL;
        ri.Printf(PRINT_ALL, "...failed to find an appropriate PIXELFORMAT\n");
        return qfalse;
	}

    // report if stereo is desired but unavailable
	if (!(pfd.dwFlags & PFD_STEREO) && (r_stereo->integer != 0)) {
		ri.Printf(PRINT_ALL, "...failed to select stereo pixel format\n");
	}

    //
    // startup the OpenGL subsystem by creating a context and making it current
    //
    ri.Printf(PRINT_ALL, "...creating GL context: ");
    gl_hglrc = qwglCreateContext(gl_hdc);
    if (gl_hglrc == NULL) {
        ReleaseDC(hwnd, gl_hdc);
		gl_hdc = NULL;
        ri.Printf(PRINT_ALL, "failed\n");
        return qfalse;
    }
    ri.Printf(PRINT_ALL, "succeeded\n");

    ri.Printf(PRINT_ALL, "...making context current: ");
    if (!qwglMakeCurrent(gl_hdc, gl_hglrc)) {
        qwglDeleteContext(gl_hglrc);
		gl_hglrc = NULL;
        ReleaseDC(hwnd, gl_hdc);
		gl_hdc = NULL;
        ri.Printf(PRINT_ALL, "failed\n");
        return qfalse;
    }
    ri.Printf(PRINT_ALL, "succeeded\n");

	glConfig.colorBits = ( int ) pfd.cColorBits;
	glConfig.depthBits = ( int ) pfd.cDepthBits;
	glConfig.stencilBits = ( int ) pfd.cStencilBits;
    glConfig.stereoEnabled = (pfd.dwFlags & PFD_STEREO) ? qtrue : qfalse;
	return qtrue;
}

static HWND create_main_window(int width, int height, qboolean fullscreen)
{
	//
	// register the window class if necessary
	//
	if (!s_main_window_class_registered)
	{
        cvar_t* cv = ri.Cvar_Get( "win_wndproc", "", 0 );
        WNDPROC	wndproc;
        sscanf(cv->string, "%p", (void **)&wndproc);

		WNDCLASS wc;

		memset( &wc, 0, sizeof( wc ) );

		wc.style         = 0;
		wc.lpfnWndProc   = wndproc;
		wc.cbClsExtra    = 0;
		wc.cbWndExtra    = 0;
		wc.hInstance     = g_wv.hInstance;
		wc.hIcon         = LoadIcon( g_wv.hInstance, MAKEINTRESOURCE(IDI_ICON1));
		wc.hCursor       = LoadCursor (NULL,IDC_ARROW);
		wc.hbrBackground = (HBRUSH) (void *)COLOR_GRAYTEXT;
		wc.lpszMenuName  = 0;
		wc.lpszClassName = MAIN_WINDOW_CLASS_NAME;

		if ( !RegisterClass( &wc ) )
		{
			ri.Error( ERR_FATAL, "create_main_window: could not register window class" );
		}
		s_main_window_class_registered = true;
		ri.Printf( PRINT_ALL, "...registered window class\n" );
	}

	//
	// compute width and height
	//
    RECT r;
	r.left = 0;
	r.top = 0;
	r.right  = width;
	r.bottom = height;

    int	stylebits;
	if ( fullscreen )
	{
		stylebits = WS_POPUP|WS_VISIBLE|WS_SYSMENU;
	}
	else
	{
		stylebits = WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_VISIBLE | WS_SYSMENU;
		AdjustWindowRect (&r, stylebits, FALSE);
	}

	int w = r.right - r.left;
	int h = r.bottom - r.top;

    int x, y;

	if ( fullscreen  )
	{
		x = 0;
		y = 0;
	}
	else
	{
		cvar_t* vid_xpos = ri.Cvar_Get ("vid_xpos", "", 0);
		cvar_t* vid_ypos = ri.Cvar_Get ("vid_ypos", "", 0);
		x = vid_xpos->integer;
		y = vid_ypos->integer;

		// adjust window coordinates if necessary 
		// so that the window is completely on screen
		if ( x < 0 )
			x = 0;
		if ( y < 0 )
			y = 0;

        int desktop_width = GetDesktopWidth();
        int desktop_height = GetDesktopHeight();

		if (w < desktop_width && h < desktop_height)
		{
			if ( x + w > desktop_width )
				x = ( desktop_width - w );
			if ( y + h > desktop_height )
				y = ( desktop_height - h );
		}
	}

	HWND hwnd = CreateWindowEx(
			0, 
			MAIN_WINDOW_CLASS_NAME,
			"Quake 3: Arena",
			stylebits,
			x, y, w, h,
			NULL,
			NULL,
			g_wv.hInstance,
			NULL);

	if (!hwnd)
	{
		ri.Error (ERR_FATAL, "create_main_window() - Couldn't create window");
	}

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	ri.Printf(PRINT_ALL, "...created window@%d,%d (%dx%d)\n", x, y, w, h);
    return hwnd;
}

static HWND create_twin_window(int width, int height, bool dx_window)
{
    //
    // register the window class if necessary
    //
    if (!s_twin_window_class_registered)
    {
        WNDCLASS wc;

        memset( &wc, 0, sizeof( wc ) );

        wc.style         = 0;
        wc.lpfnWndProc   = DefWindowProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = g_wv.hInstance;
        wc.hIcon         = LoadIcon( g_wv.hInstance, MAKEINTRESOURCE(IDI_ICON1));
        wc.hCursor       = LoadCursor (NULL,IDC_ARROW);
        wc.hbrBackground = (HBRUSH) (void *)COLOR_GRAYTEXT;
        wc.lpszMenuName  = 0;
        wc.lpszClassName = TWIN_WINDOW_CLASS_NAME;

        if ( !RegisterClass( &wc ) )
        {
            ri.Error( ERR_FATAL, "create_twin_window: could not register window class" );
        }
		s_twin_window_class_registered = true;
        ri.Printf( PRINT_ALL, "...registered twin window class\n" );
    }

    //
    // compute width and height
    //
    RECT r;
    r.left = 0;
    r.top = 0;
    r.right  = width;
    r.bottom = height;

    int stylebits = WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_VISIBLE | WS_SYSMENU;
    AdjustWindowRect (&r, stylebits, FALSE);

    int w = r.right - r.left;
    int h = r.bottom - r.top;

    cvar_t* vid_xpos = ri.Cvar_Get ("vid_xpos", "", 0);
    cvar_t* vid_ypos = ri.Cvar_Get ("vid_ypos", "", 0);
    int x = vid_xpos->integer + width + 5; // offset to the right of the main window
    int y = vid_ypos->integer;

    // adjust window coordinates if necessary 
    // so that the window is completely on screen
    if ( x < 0 )
        x = 0;
    if ( y < 0 )
        y = 0;

    int desktop_width = GetDesktopWidth();
    int desktop_height = GetDesktopHeight();

    if (w < desktop_width && h < desktop_height)
    {
        if ( x + w > desktop_width )
            x = ( desktop_width - w );
        if ( y + h > desktop_height )
            y = ( desktop_height - h );
    }

    // If r_renderAPI = 0 (OpenGL) then twin window uses Vulkan API.
    // If r_renderAPI = 1 (Vulkan) then twin window uses OpenGL API.
    char window_name[1024];
    sprintf(window_name, "%s [%s]", MAIN_WINDOW_CLASS_NAME, dx_window ? "DX12" : (r_renderAPI->integer == 0 ? "Vulkan" : "OpenGL"));

    HWND hwnd = CreateWindowEx(
        0, 
        TWIN_WINDOW_CLASS_NAME,
        window_name,
        stylebits,
        x, y, w, h,
        NULL,
        NULL,
        g_wv.hInstance,
        NULL);

    if (!hwnd)
    {
        ri.Error (ERR_FATAL, "create_twin_window() - Couldn't create window");
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    ri.Printf(PRINT_ALL, "...created twin window@%d,%d (%dx%d)\n", x, y, w, h);
    return hwnd;
}

static void SetMode(int mode, qboolean fullscreen) {
	if (fullscreen) {
		ri.Printf( PRINT_ALL, "...setting fullscreen mode:");
		glConfig.vidWidth = GetDesktopWidth();
		glConfig.vidHeight = GetDesktopHeight();
		glConfig.windowAspect = 1.0f;
	} else {
		ri.Printf( PRINT_ALL, "...setting mode %d:", mode );
		if (!R_GetModeInfo(&glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, mode)) {
			ri.Printf( PRINT_ALL, " invalid mode\n" );
			ri.Error(ERR_FATAL, "SetMode - could not set the given mode (%d)\n", mode);
		}

		// Ensure that window size does not exceed desktop size.
		// CreateWindow Win32 API does not allow to create windows larger than desktop.
		int desktop_width = GetDesktopWidth();
		int desktop_height = GetDesktopHeight();

		if (glConfig.vidWidth > desktop_width || glConfig.vidHeight > desktop_height) {
			int default_mode = 4;
			ri.Printf(PRINT_WARNING, "\nMode %d specifies width that is larger than desktop width: using default mode %d\n", mode, default_mode);
			
			ri.Printf( PRINT_ALL, "...setting mode %d:", default_mode );
			if (!R_GetModeInfo(&glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, default_mode)) {
				ri.Printf( PRINT_ALL, " invalid mode\n" );
				ri.Error(ERR_FATAL, "SetMode - could not set the given mode (%d)\n", default_mode);
			}
		}
	}
	glConfig.isFullscreen = fullscreen;
	ri.Printf( PRINT_ALL, " %d %d %s\n", glConfig.vidWidth, glConfig.vidHeight, fullscreen ? "FS" : "W");
}

/*
** GLW_InitExtensions
*/
static void GLW_InitExtensions( void )
{
	ri.Printf( PRINT_ALL, "Initializing OpenGL extensions\n" );

	// GL_S3_s3tc
	glConfig.textureCompression = TC_NONE;
	if ( strstr( glConfig.extensions_string, "GL_S3_s3tc" ) )
	{
		if ( r_ext_compressed_textures->integer )
		{
			glConfig.textureCompression = TC_S3TC;
			ri.Printf( PRINT_ALL, "...using GL_S3_s3tc\n" );
		}
		else
		{
			glConfig.textureCompression = TC_NONE;
			ri.Printf( PRINT_ALL, "...ignoring GL_S3_s3tc\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_S3_s3tc not found\n" );
	}

	// GL_EXT_texture_env_add
	glConfig.textureEnvAddAvailable = qfalse;
	if ( strstr( glConfig.extensions_string, "EXT_texture_env_add" ) )
	{
		if ( r_ext_texture_env_add->integer )
		{
			glConfig.textureEnvAddAvailable = qtrue;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_env_add\n" );
		}
		else
		{
			glConfig.textureEnvAddAvailable = qfalse;
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_env_add\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_env_add not found\n" );
	}

	// WGL_EXT_swap_control
	qwglSwapIntervalEXT = ( BOOL (WINAPI *)(int)) qwglGetProcAddress( "wglSwapIntervalEXT" );
	if ( qwglSwapIntervalEXT )
	{
		ri.Printf( PRINT_ALL, "...using WGL_EXT_swap_control\n" );
		r_swapInterval->modified = qtrue;	// force a set next frame
	}
	else
	{
		ri.Printf( PRINT_ALL, "...WGL_EXT_swap_control not found\n" );
	}

    // GL_ARB_multitexture
    {
        if (!strstr(glConfig.extensions_string, "GL_ARB_multitexture"))
            ri.Error(ERR_FATAL, "GL_ARB_multitexture not found");

        qglActiveTextureARB = ( void (APIENTRY * ) (GLenum target) ) qwglGetProcAddress("glActiveTextureARB");
        qglClientActiveTextureARB = ( void (APIENTRY * ) (GLenum target) ) qwglGetProcAddress("glClientActiveTextureARB");

        if (!qglActiveTextureARB || !qglClientActiveTextureARB)
            ri.Error(ERR_FATAL, "GL_ARB_multitexture: could not initialize function pointers");

        qglGetIntegerv(GL_MAX_ACTIVE_TEXTURES_ARB, &glConfig.maxActiveTextures);

        if (glConfig.maxActiveTextures < 2)
            ri.Error(ERR_FATAL, "GL_ARB_multitexture: < 2 texture units");

        ri.Printf(PRINT_ALL, "...using GL_ARB_multitexture\n");
    }

	// GL_EXT_compiled_vertex_array
	qglLockArraysEXT = NULL;
	qglUnlockArraysEXT = NULL;
	if ( strstr( glConfig.extensions_string, "GL_EXT_compiled_vertex_array" ) )
	{
		if ( r_ext_compiled_vertex_array->integer )
		{
			ri.Printf( PRINT_ALL, "...using GL_EXT_compiled_vertex_array\n" );
			qglLockArraysEXT = ( void ( APIENTRY * )( int, int ) ) qwglGetProcAddress( "glLockArraysEXT" );
			qglUnlockArraysEXT = ( void ( APIENTRY * )( void ) ) qwglGetProcAddress( "glUnlockArraysEXT" );
			if (!qglLockArraysEXT || !qglUnlockArraysEXT) {
				ri.Error (ERR_FATAL, "bad getprocaddress");
			}
		}
		else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_compiled_vertex_array\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_compiled_vertex_array not found\n" );
	}
}

/*
** GLimp_EndFrame
*/
void GLimp_EndFrame (void)
{
	if (!gl_active)
		return;

	//
	// swapinterval stuff
	//
	if ( r_swapInterval->modified ) {
		r_swapInterval->modified = qfalse;

		if ( !glConfig.stereoEnabled ) {	// why?
			if ( qwglSwapIntervalEXT ) {
				qwglSwapIntervalEXT( r_swapInterval->integer );
			}
		}
	}


	// don't flip if drawing to front buffer
	if ( Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) != 0 )
	{
			SwapBuffers( gl_hdc );
	}

	// check logging
	QGL_EnableLogging( (qboolean) r_logFile->integer );
}

/*
** GLimp_Init
**
** This is the platform specific OpenGL initialization function.  It
** is responsible for loading OpenGL, initializing it, setting
** extensions, creating a window of the appropriate size, doing
** fullscreen manipulations, etc.  Its overall responsibility is
** to make sure that a functional OpenGL subsystem is operating
** when it returns to the ref.
*/
void GLimp_Init( void )
{
	ri.Printf( PRINT_ALL, "Initializing OpenGL subsystem\n" );

	// load appropriate DLL and initialize subsystem
    //
    // load the driver and bind our function pointers to it
    // 
    if (!QGL_Init(r_glDriver->string))
    {
        ri.Error(ERR_FATAL, "QGL_Init - could not load OpenGL driver\n");
    }

	SetMode(r_mode->integer, (qboolean)r_fullscreen->integer);

	if (r_renderAPI->integer == 0) {
		g_wv.hWnd_opengl = create_main_window(glConfig.vidWidth, glConfig.vidHeight, (qboolean)r_fullscreen->integer);
		g_wv.hWnd = g_wv.hWnd_opengl;
		SetForegroundWindow(g_wv.hWnd);
		SetFocus(g_wv.hWnd);
		WG_CheckHardwareGamma();
	} else {
		g_wv.hWnd_opengl = create_twin_window(glConfig.vidWidth, glConfig.vidHeight, false);
	}

	if (!GLW_InitDriver(g_wv.hWnd_opengl)) {
		ri.Error(ERR_FATAL, "GLW_InitDriver - could not initialize OpenGL subsystem\n");
	}

	// get our config strings
	Q_strncpyz( glConfig.vendor_string, (const char*) qglGetString (GL_VENDOR), sizeof( glConfig.vendor_string ) );
	Q_strncpyz(glConfig.renderer_string, (const char*)qglGetString(GL_RENDERER), sizeof(glConfig.renderer_string));
	Q_strncpyz(glConfig.version_string, (const char*)qglGetString(GL_VERSION), sizeof(glConfig.version_string));
	Q_strncpyz(glConfig.extensions_string, (const char*)qglGetString(GL_EXTENSIONS), sizeof(glConfig.extensions_string));

	GLW_InitExtensions();
}

/*
** GLimp_Shutdown
**
** This routine does all OS specific shutdown procedures for the OpenGL
** subsystem.
*/
void GLimp_Shutdown( void )
{
	const char *success[] = { "failed", "success" };

	ri.Printf(PRINT_ALL, "Shutting down OpenGL subsystem\n");

	if (qwglMakeCurrent) {
		int retVal = qwglMakeCurrent(NULL, NULL) != 0;
		ri.Printf(PRINT_ALL, "...wglMakeCurrent( NULL, NULL ): %s\n", success[retVal]);
	}

	if (gl_hglrc) {
		int retVal = qwglDeleteContext(gl_hglrc) != 0;
		ri.Printf(PRINT_ALL, "...deleting GL context: %s\n", success[retVal]);
		gl_hglrc = NULL;
	}

	if (gl_hdc) {
		int retVal = ReleaseDC(g_wv.hWnd_opengl, gl_hdc) != 0;
		ri.Printf(PRINT_ALL, "...releasing DC: %s\n", success[retVal]);
		gl_hdc = NULL;
	}

	// destroy window
	if (g_wv.hWnd_opengl) {
		ri.Printf(PRINT_ALL, "...destroying opengl window\n");
		ShowWindow(g_wv.hWnd_opengl, SW_HIDE);
		DestroyWindow(g_wv.hWnd_opengl);

		if (g_wv.hWnd == g_wv.hWnd_opengl) {
			g_wv.hWnd = NULL;
		}
		g_wv.hWnd_opengl = NULL;
	}

	QGL_Shutdown();

	WG_RestoreGamma();

	gl_active = false;
	memset(&glConfig, 0, sizeof(glConfig));
	memset(&glState, 0, sizeof(glState));

	if (log_fp) {
		fclose(log_fp);
		log_fp = 0;
	}
}

void GLimp_LogComment( char *comment ) 
{
	if ( log_fp ) {
		fprintf( log_fp, "%s", comment );
	}
}

void vk_imp_init() {
	ri.Printf(PRINT_ALL, "Initializing Vulkan subsystem\n");

	// This will set qgl pointers to no-op placeholders.
	if (!gl_active) {
		QGL_Init(nullptr);
		qglActiveTextureARB = [] (GLenum)  {};
		qglClientActiveTextureARB = [](GLenum) {};
	}

	// Load Vulkan DLL.
	const char* dll_name = "vulkan-1.dll";

	ri.Printf(PRINT_ALL, "...calling LoadLibrary('%s'): ", dll_name);
	vk_library_handle = LoadLibrary(dll_name);

	if (vk_library_handle == NULL) {
		ri.Printf(PRINT_ALL, "failed\n");
		ri.Error(ERR_FATAL, "vk_imp_init - could not load %s\n", dll_name);
	}
	ri.Printf( PRINT_ALL, "succeeded\n" );

	vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(vk_library_handle, "vkGetInstanceProcAddr");

	// Create window.
	SetMode(r_mode->integer, (qboolean)r_fullscreen->integer);

	if (r_renderAPI->integer != 0) {
		g_wv.hWnd_vulkan = create_main_window(glConfig.vidWidth, glConfig.vidHeight, (qboolean)r_fullscreen->integer);
		g_wv.hWnd = g_wv.hWnd_vulkan;
		SetForegroundWindow(g_wv.hWnd);
		SetFocus(g_wv.hWnd);
		WG_CheckHardwareGamma();

		g_wv.hWnd_dx = create_twin_window(glConfig.vidWidth, glConfig.vidHeight, true);
	} else {
		g_wv.hWnd_vulkan = create_twin_window(glConfig.vidWidth, glConfig.vidHeight, false);
	}
}

void vk_imp_shutdown() {
	ri.Printf(PRINT_ALL, "Shutting down Vulkan subsystem\n");

	if (g_wv.hWnd_vulkan) {
		ri.Printf(PRINT_ALL, "...destroying Vulkan window\n");
		DestroyWindow(g_wv.hWnd_vulkan);

		if (g_wv.hWnd == g_wv.hWnd_vulkan) {
			g_wv.hWnd = NULL;
		}
		g_wv.hWnd_vulkan = NULL;
	}

	if (vk_library_handle != NULL) {
		ri.Printf(PRINT_ALL, "...unloading Vulkan DLL\n");
		FreeLibrary(vk_library_handle);
		vk_library_handle = NULL;
	}
	vkGetInstanceProcAddr = nullptr;

	if (g_wv.hWnd_dx) {
		ri.Printf(PRINT_ALL, "...destroying DX12 window\n");
		DestroyWindow(g_wv.hWnd_dx);

		if (g_wv.hWnd == g_wv.hWnd_dx) {
			g_wv.hWnd_dx = NULL;
		}
		g_wv.hWnd_dx = NULL;
	}

	// For vulkan mode we still have qgl pointers initialized with placeholder values.
	// Reset them the same way as we do in opengl mode.
	QGL_Shutdown();

	WG_RestoreGamma();

	memset(&glConfig, 0, sizeof(glConfig));
	memset(&glState, 0, sizeof(glState));

	if (log_fp) {
		fclose(log_fp);
		log_fp = 0;
	}
}

void vk_imp_create_surface() {
	VkWin32SurfaceCreateInfoKHR desc;
	desc.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	desc.pNext = nullptr;
	desc.flags = 0;
	desc.hinstance = ::GetModuleHandle(nullptr);
	desc.hwnd = g_wv.hWnd_vulkan;
	VK_CHECK(vkCreateWin32SurfaceKHR(vk.instance, &desc, nullptr, &vk.surface));
}

/*
===========================================================

SMP acceleration

===========================================================
*/

HANDLE	renderCommandsEvent;
HANDLE	renderCompletedEvent;
HANDLE	renderActiveEvent;

void (*glimpRenderThread)( void );

void GLimp_RenderThreadWrapper( void ) {
	glimpRenderThread();

	// unbind the context before we die
	qwglMakeCurrent( gl_hdc, NULL );
}

/*
=======================
GLimp_SpawnRenderThread
=======================
*/
HANDLE	renderThreadHandle;
int		renderThreadId;
qboolean GLimp_SpawnRenderThread( void (*function)( void ) ) {

	renderCommandsEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
	renderCompletedEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
	renderActiveEvent = CreateEvent( NULL, TRUE, FALSE, NULL );

	glimpRenderThread = function;

	renderThreadHandle = CreateThread(
	   NULL,	// LPSECURITY_ATTRIBUTES lpsa,
	   0,		// DWORD cbStack,
	   (LPTHREAD_START_ROUTINE)GLimp_RenderThreadWrapper,	// LPTHREAD_START_ROUTINE lpStartAddr,
	   0,			// LPVOID lpvThreadParm,
	   0,			//   DWORD fdwCreate,
	   (LPDWORD)&renderThreadId );

	if ( !renderThreadHandle ) {
		return qfalse;
	}

	return qtrue;
}

static	void	*smpData;
static	int		wglErrors;

void *GLimp_RendererSleep( void ) {
	void	*data;

	if ( !qwglMakeCurrent( gl_hdc, NULL ) ) {
		wglErrors++;
	}

	ResetEvent( renderActiveEvent );

	// after this, the front end can exit GLimp_FrontEndSleep
	SetEvent( renderCompletedEvent );

	WaitForSingleObject( renderCommandsEvent, INFINITE );

	if ( !qwglMakeCurrent( gl_hdc, gl_hglrc ) ) {
		wglErrors++;
	}

	ResetEvent( renderCompletedEvent );
	ResetEvent( renderCommandsEvent );

	data = smpData;

	// after this, the main thread can exit GLimp_WakeRenderer
	SetEvent( renderActiveEvent );

	return data;
}


void GLimp_FrontEndSleep( void ) {
	WaitForSingleObject( renderCompletedEvent, INFINITE );

	if ( !qwglMakeCurrent( gl_hdc, gl_hglrc ) ) {
		wglErrors++;
	}
}


void GLimp_WakeRenderer( void *data ) {
	smpData = data;

	if ( !qwglMakeCurrent( gl_hdc, NULL ) ) {
		wglErrors++;
	}

	// after this, the renderer can continue through GLimp_RendererSleep
	SetEvent( renderCommandsEvent );

	WaitForSingleObject( renderActiveEvent, INFINITE );
}

