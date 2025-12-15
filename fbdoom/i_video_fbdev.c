// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// $Log:$
//
// DESCRIPTION:
//	DOOM graphics stuff for X11, UNIX.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include "config.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_event.h"
#include "d_main.h"
#include "i_video.h"
#include "z_zone.h"

#include "tables.h"
#include "doomkeys.h"

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>

#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

//#define CMAP256

static struct fb_var_screeninfo fb;
static struct fb_fix_screeninfo finfo;
int usemouse = 0;

struct color {
    uint32_t b:8;
    uint32_t g:8;
    uint32_t r:8;
    uint32_t a:8;
};

static struct color colors[256];

// The screen buffer; this is modified to draw things to the screen

byte *I_VideoBuffer = NULL;
static char *fbp = 0;

/* framebuffer file descriptor */
int fd_fb = 0;

int	X_width;
int X_height;

// If true, game is running as a screensaver

boolean screensaver_mode = false;

// Flag indicating whether the screen is currently visible:
// when the screen isnt visible, don't render the screen

boolean screenvisible;

// Mouse acceleration
//
// This emulates some of the behavior of DOS mouse drivers by increasing
// the speed when the mouse is moved fast.
//
// The mouse input values are input directly to the game, but when
// the values exceed the value of mouse_threshold, they are multiplied
// by mouse_acceleration to increase the speed.

float mouse_acceleration = 2.0;
int mouse_threshold = 10;

// Gamma correction level to use

int usegamma = 0;

typedef struct
{
	byte r;
	byte g;
	byte b;
} col_t;

// Palette converted to RGB565

static uint16_t rgb565_palette[256];

void I_InitGraphics (void)
{
    int i;

    /* Open fbdev file descriptor */
    fd_fb = open("/dev/fb0", O_RDWR);
    if (fd_fb < 0)
    {
        printf("Could not open /dev/fb0");
        exit(-1);
    }

	if (ioctl(fd_fb, FBIOGET_FSCREENINFO, &finfo)) {
        printf("Error reading fixed information.\n");
            exit(2);
    }
	
    /* fetch framebuffer info */
    ioctl(fd_fb, FBIOGET_VSCREENINFO, &fb);
    /* change params if needed */
    //ioctl(fd_fb, FBIOPUT_VSCREENINFO, &fb);
    printf("I_InitGraphics: framebuffer: x_res: %d, y_res: %d, x_virtual: %d, y_virtual: %d, bpp: %d, grayscale: %d\n",
            fb.xres, fb.yres, fb.xres_virtual, fb.yres_virtual, fb.bits_per_pixel, fb.grayscale);

    printf("I_InitGraphics: framebuffer: RGBA: %d%d%d%d, red_off: %d, green_off: %d, blue_off: %d, transp_off: %d\n",
            fb.red.length, fb.green.length, fb.blue.length, fb.transp.length, fb.red.offset, fb.green.offset, fb.blue.offset, fb.transp.offset);

    printf("I_InitGraphics: DOOM screen size: w x h: %d x %d\n", SCREENWIDTH, SCREENHEIGHT);

	fbp = (char *)mmap(0, fb.xres * fb.yres * (fb.bits_per_pixel/8), PROT_READ | PROT_WRITE, MAP_SHARED,fd_fb, 0);
    if ((int64_t)fbp == -1) {
            printf("Error: failed to map framebuffer device to memory.\n");
            exit(4);
    }

    /* Allocate screen to draw to */
	I_VideoBuffer = (byte*)Z_Malloc (SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);  // For DOOM to draw on

	screenvisible = true;

    extern int I_InitInput(void);
    I_InitInput();
}

void I_ShutdownGraphics (void)
{
	Z_Free (I_VideoBuffer);
	munmap(fbp, fb.xres * fb.yres * (fb.bits_per_pixel/8));
}

void I_StartFrame (void)
{

}

__attribute__ ((weak)) void I_GetEvent (void)
{

}

__attribute__ ((weak)) void I_StartTic (void)
{
	I_GetEvent();
}

void I_UpdateNoBlit (void)
{
}

int location(int x, int y)
{
    return (x+fb.xoffset) * (fb.bits_per_pixel/8) + (y+fb.yoffset) * finfo.line_length;
}

uint16_t colorTo16bit(struct color col)
{
    return  (col.r >> 3) << 11 | (col.g >> 2) << 5 | (col.b >> 3);
}

//
// I_FinishUpdate
//

void I_FinishUpdate (void)
{
	float y_scale = (float)fb.yres / SCREENHEIGHT;
    float x_scale = (float)fb.xres / SCREENWIDTH;
    float scale = (y_scale < x_scale) ? y_scale : x_scale;
    float y_offset = (((float)fb.yres - SCREENHEIGHT * scale) / 2.0);
    float x_offset = (((float)fb.xres - SCREENWIDTH * scale) / 2.0);

    for (int gy = 0; gy < SCREENHEIGHT; gy++)
    {
        for (int gx = 0; gx < SCREENWIDTH; gx++)
        {
            int fb_y = (int)((float)gy * scale + y_offset);
            int fb_x = (int)((float)gx * scale + x_offset);
			int b = (int)fb_x;
			int a = (int)fb_y;
			if (fb_y - (float)a >= 0.5)
				a = 1.0;
			if (fb_x - (float)b >= 0.5)
				b = 1.0;
            if (fb_y < 0 || fb_y >= fb.yres || fb_x < 0 || fb_x >= fb.xres)
                continue;

            int fbPos = location(fb_x, fb_y);
            uint8_t color_idx = *(I_VideoBuffer + gy * SCREENWIDTH + gx);
            uint16_t pixel = colorTo16bit(colors[color_idx]);
            *((uint16_t *)(fbp + fbPos)) = pixel;

			if (a == 1.0)
				fbPos = location(fb_x, fb_y+1);
				*((uint16_t *)(fbp + fbPos)) = pixel;
			if (b == 1.0)
				fbPos = location(fb_x+1, fb_y);
				*((uint16_t *)(fbp + fbPos)) = pixel;
			if (a== 1.0 && b == 1.0)
				fbPos = location(fb_x+1, fb_y+1);
				*((uint16_t *)(fbp + fbPos)) = pixel;
        }
    }
}

//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
    memcpy (scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}

//
// I_SetPalette
//
#define GFX_RGB565(r, g, b)			((((r & 0xF8) >> 3) << 11) | (((g & 0xFC) >> 2) << 5) | ((b & 0xF8) >> 3))
#define GFX_RGB565_R(color)			((0xF800 & color) >> 11)
#define GFX_RGB565_G(color)			((0x07E0 & color) >> 5)
#define GFX_RGB565_B(color)			(0x001F & color)

void I_SetPalette (byte* palette)
{
	int i;
	//col_t* c;

	//for (i = 0; i < 256; i++)
	//{
	//	c = (col_t*)palette;

	//	rgb565_palette[i] = GFX_RGB565(gammatable[usegamma][c->r],
	//								   gammatable[usegamma][c->g],
	//								   gammatable[usegamma][c->b]);

	//	palette += 3;
	//}
    

    /* performance boost:
     * map to the right pixel format over here! */

    for (i=0; i<256; ++i ) {
        colors[i].a = 0;
        colors[i].r = gammatable[usegamma][*palette++];
        colors[i].g = gammatable[usegamma][*palette++];
        colors[i].b = gammatable[usegamma][*palette++];
    }

    /* Set new color map in kernel framebuffer driver */
    //XXX FIXME ioctl(fd_fb, IOCTL_FB_PUTCMAP, colors);
}

// Given an RGB value, find the closest matching palette index.

int I_GetPaletteIndex (int r, int g, int b)
{
    int best, best_diff, diff;
    int i;
    col_t color;

    printf("I_GetPaletteIndex\n");

    best = 0;
    best_diff = INT_MAX;

    for (i = 0; i < 256; ++i)
    {
    	color.r = GFX_RGB565_R(rgb565_palette[i]);
    	color.g = GFX_RGB565_G(rgb565_palette[i]);
    	color.b = GFX_RGB565_B(rgb565_palette[i]);

        diff = (r - color.r) * (r - color.r)
             + (g - color.g) * (g - color.g)
             + (b - color.b) * (b - color.b);

        if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
        }

        if (diff == 0)
        {
            break;
        }
    }

    return best;
}

void I_BeginRead (void)
{
}

void I_EndRead (void)
{
}

void I_SetWindowTitle (char *title)
{
}

void I_GraphicsCheckCommandLine (void)
{
}

void I_SetGrabMouseCallback (grabmouse_callback_t func)
{
}

void I_EnableLoadingDisk(void)
{
}

void I_BindVideoVariables (void)
{
}

void I_DisplayFPSDots (boolean dots_on)
{
}

void I_CheckIsScreensaver (void)
{
}
