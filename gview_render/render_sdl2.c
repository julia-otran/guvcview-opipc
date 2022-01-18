/*******************************************************************************#
#           guvcview              http://guvcview.sourceforge.net               #
#                                                                               #
#           Paulo Assis <pj.assis@gmail.com>                                    #
#           Nobuhiro Iwamatsu <iwamatsu@nigauri.org>                            #
#                             Add UYVY color support(Macbook iSight)            #
#           Flemming Frandsen <dren.dk@gmail.com>                               #
#                             Add VU meter OSD                                  #
#                                                                               #
# This program is free software; you can redistribute it and/or modify          #
# it under the terms of the GNU General Public License as published by          #
# the Free Software Foundation; either version 2 of the License, or             #
# (at your option) any later version.                                           #
#                                                                               #
# This program is distributed in the hope that it will be useful,               #
# but WITHOUT ANY WARRANTY; without even the implied warranty of                #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                 #
# GNU General Public License for more details.                                  #
#                                                                               #
# You should have received a copy of the GNU General Public License             #
# along with this program; if not, write to the Free Software                   #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA     #
#                                                                               #
********************************************************************************/

#include <SDL.h>
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <turbojpeg.h>
#include <arm_neon.h>
#include <stdio.h>

#include "gview.h"
#include "gviewrender.h"
#include "render.h"
#include "render_sdl2.h"
#include "../config.h"

extern int verbosity;

SDL_DisplayMode display_mode;

static SDL_Window*  sdl_window = NULL;
static SDL_Texture* rending_texture = NULL;
static SDL_Renderer*  main_renderer = NULL;
static SDL_Surface* sdl_surface = NULL;
pthread_t thread_id;

uint8_t *frame_ptr;
int frame_width;
int frame_height;
int run = 1;

int sdl_init_flags;

#define LOAD_Y(i,j) (pY + i * width + j)
#define LOAD_V(i,j) (pV + i * (width / 2) + (j / 2))
#define LOAD_U(i,j) (pU + i * (width / 2) + (j / 2))

const uint8_t ZEROS[8] = {220,220, 220, 220, 220, 220, 220, 220};
const uint8_t Y_SUBS[8] = {16, 16, 16, 16, 16, 16, 16, 16};
const uint8_t UV_SUBS[8] = {128, 128, 128, 128, 128, 128, 128, 128};

const uint8_t COLOR_MAX[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
const uint8_t COLOR_MIN[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

const uint8_t ALPHAS[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

void color_convert_common(unsigned char *pY, unsigned char *pU, unsigned char *pV, int width, int height, unsigned char *buffer, int grey)
{

  int i, j;
  unsigned char *out = buffer;

  int16x8_t Y_SUBvec = (int16x8_t) vmovl_u8(vld1_u8(Y_SUBS));
  int16x8_t UV_SUBvec = (int16x8_t) vmovl_u8(vld1_u8(UV_SUBS)); // v,u,v,u v,u,v,u
  
  int16x8_t COLOR_MAXvec = (int16x8_t) vmovl_u8(vld1_u8(COLOR_MAX));
  int16x8_t COLOR_MINvec = (int16x8_t) vmovl_u8(vld1_u8(COLOR_MIN));

  uint8x8_t ZEROSvec =vld1_u8(ZEROS);
  uint8x8_t ALPHAvec = vld1_u8(ALPHAS);
 
  int16x8_t nUvec;
  int16x8_t nVvec;

    // YUV 4:2:2
    for (i = 0; i < height; i++)
    {
      for (j = 0; j < width; j += 8)
      {
        //        nY = *(pY + i * width + j);
        //        nV = *(pUV + (i / 2) * width + bytes_per_pixel * (j / 2));
        //        nU = *(pUV + (i / 2) * width + bytes_per_pixel * (j / 2) + 1);

        uint8x8_t nYvec;
        uint8x16_t nYLoadvec;
	uint8x16_t nULoadvec;
	uint8x16_t nVLoadvec;
	uint8x8_t nTempUvec;
	uint8x8_t nTempVvec;

        int16x4_t nUvec16_4;
        int16x4_t nVvec16_4;

	if (j % 16 == 0) {
	    nYLoadvec = vld1q_u8(LOAD_Y(i,j));
	    nYvec = vget_low_u8(nYLoadvec);

	    if (j % 32 == 0) {
	        nULoadvec = vld1q_u8(LOAD_U(i,j));
		nVLoadvec = vld1q_u8(LOAD_V(i,j));
		nTempUvec = vget_low_u8(nULoadvec);
		nTempVvec = vget_low_u8(nVLoadvec);
	    } else {
		nTempUvec = vget_high_u8(nULoadvec);
		nTempVvec = vget_high_u8(nVLoadvec);
	    }

	    nUvec = (int16x8_t) vmovl_u8(nTempUvec);
	    nUvec = vsubq_s16(nUvec, UV_SUBvec);
	    nUvec16_4 = vget_low_s16(nUvec);

	    nVvec = (int16x8_t) vmovl_u8(nTempVvec);
	    nVvec = vsubq_s16(nVvec, UV_SUBvec);
            nVvec16_4 = vget_low_s16(nVvec);	    
        } else {
	    nYvec = vget_high_u8(nYLoadvec);
            nUvec16_4 = vget_high_s16(nUvec);
            nVvec16_4 = vget_high_s16(nVvec);
	}

	int32x4_t nUvec32_4 = vmovl_s16(nUvec16_4);
	int32x4_t nVvec32_4 = vmovl_s16(nVvec16_4);

        // uint8x8_t nUVvec = vld1_u8(LOAD_V(i,j)); // v,u,v,u v,u,v,u
	// uint16x8_t nUVvec = vzip_u8(nUvec, nVvec);

        // nYvec = vmul_u8(nYvec, vcle_u8(nYvec,ZEROSvec));

        // Yuv Convert
        //        nY -= 16;
        //        nU -= 128;
        //        nV -= 128;

        //        nYvec = vsub_u8(nYvec, Y_SUBvec);
        //        nUVvec = vsub_u8(nYvec, UV_SUBvec);

        // int16x8_t nYvec16 = vsubq_s16((int16x8_t) vmovl_u8(nYvec), Y_SUBvec);
	int16x8_t nYvec16 = (int16x8_t) vmovl_u8(nYvec);
        // uint16x8_t nUVvec16 = vmovl_u8(vsub_u8(nUVvec, UV_SUBvec));

        int16x4_t Y_low4 = vget_low_s16(nYvec16);
        int16x4_t Y_high4 = vget_high_s16(nYvec16);
        
	// uint16x4_t UV_low4 = vget_low_u16(nUVvec16);
	// uint16x2 U_low2 = vget_low_u16(nUvec8);
	// uint16x2 V_low2 = vget_low_u16(nVvec8);

        // uint16x4_t UV_high4 = vget_high_u16(nUVvec16);
	// uint16x2 U_high2 = vget_high_u16(nUvec8);
	// uint16x2 V_high2 = vget_high_u16(nVvec8);

        // uint32x4_t UV_low4_int = vmovl_u16(UV_low4);
        // uint32x4_t UV_high4_int = vmovl_u16(UV_high4);

        int32x4_t Y_low4_int = vmull_n_s16(Y_low4, 1000);
        int32x4_t Y_high4_int = vmull_n_s16(Y_high4, 1000);

        // uint32x4x2_t UV_uzp = vuzpq_u32(UV_low4_int, UV_high4_int);

        // uint32x2_t Vl = vget_low_u32(UV_uzp.val[0]);// vld1_u32(UVvec_int);
        // uint32x2_t Vh = vget_high_u32(UV_uzp.val[0]);//vld1_u32(UVvec_int + 2);

	int32x2_t Vl = vget_low_s32(nVvec32_4);// vld1_u32(UVvec_int);
        int32x2_t Vh = vget_high_s32(nVvec32_4);
	
        int32x2x2_t Vll_ = vzip_s32(Vl, Vl);
        int32x4_t* Vll = (uint32x4_t*)(&Vll_);

        int32x2x2_t Vhh_ = vzip_s32(Vh, Vh);
        int32x4_t* Vhh = (uint32x4_t*)(&Vhh_);

        int32x2_t Ul =  vget_low_s32(nUvec32_4);
        int32x2_t Uh =  vget_high_s32(nUvec32_4);

        int32x2x2_t Ull_ = vzip_s32(Ul, Ul);
        int32x4_t* Ull = (uint32x4_t*)(&Ull_);

        int32x2x2_t Uhh_ = vzip_s32(Uh, Uh);
        int32x4_t* Uhh = (uint32x4_t*)(&Uhh_);

        int32x4_t B_int_low = vmlaq_n_s32(Y_low4_int, *Ull, 1402); //multiply by scalar accum
        int32x4_t B_int_high = vmlaq_n_s32(Y_high4_int, *Uhh, 1402); //multiply by scalar accum
        int32x4_t G_int_low = vsubq_s32(Y_low4_int, vmlaq_n_s32(vmulq_n_s32(*Vll, 714), *Ull, 344));
        int32x4_t G_int_high = vsubq_s32(Y_high4_int, vmlaq_n_s32(vmulq_n_s32(*Vhh, 714), *Uhh, 344));
        int32x4_t R_int_low = vmlaq_n_s32(Y_low4_int, *Vll, 1772); //multiply by scalar accum
        int32x4_t R_int_high = vmlaq_n_s32(Y_high4_int, *Vhh, 1772); //multiply by scalar accum

        B_int_low = vshrq_n_s32 (B_int_low, 10);
        B_int_high = vshrq_n_s32 (B_int_high, 10);
        G_int_low = vshrq_n_s32 (G_int_low, 10);
        G_int_high = vshrq_n_s32 (G_int_high, 10);
        R_int_low = vshrq_n_s32 (R_int_low, 10);
        R_int_high = vshrq_n_s32 (R_int_high, 10);


	int16x8_t R_combined = vcombine_s16(vqmovn_s32 (R_int_low),vqmovn_s32 (R_int_high));
	int16x8_t G_combined = vcombine_s16(vqmovn_s32 (G_int_low),vqmovn_s32 (G_int_high));
	int16x8_t B_combined = vcombine_s16(vqmovn_s32 (B_int_low),vqmovn_s32 (B_int_high));

	R_combined = vmaxq_s16(vminq_s16(R_combined, COLOR_MAXvec), COLOR_MINvec);
	G_combined = vmaxq_s16(vminq_s16(G_combined, COLOR_MAXvec), COLOR_MINvec);
	B_combined = vmaxq_s16(vminq_s16(B_combined, COLOR_MAXvec), COLOR_MINvec);

        uint8x8x4_t RGB;
        RGB.val[0] = vmovn_u16((uint16x8_t) R_combined);
        RGB.val[1] = vmovn_u16((uint16x8_t) G_combined);
        RGB.val[2] = vmovn_u16((uint16x8_t) B_combined);
	RGB.val[3] = ALPHAvec;

        vst4_u8 (out+i*width*4 + j*4, RGB);
      }
    }
}

/*
 * initialize sdl video
 * args:
 *   width - video width
 *   height - video height
 *   flags - window flags:
 *              0- none
 *              1- fullscreen
 *              2- maximized
 *
 * asserts:
 *   none
 *
 * returns: error code
 */

static int video_init2(int width, int height, int flags)
{
	int w = width;
	int h = height;
	int32_t my_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;

	switch(flags)
	{
		case 2:
		  my_flags |= SDL_WINDOW_MAXIMIZED;
		  break;
		case 1:
		  my_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
		  break;
		case 0:
		default:
		  break;
	}

	if(verbosity > 0)
		printf("RENDER: Initializing SDL2 render\n");

    if (sdl_window == NULL) /*init SDL*/
    {
        if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER) < 0)
        {
            fprintf(stderr, "RENDER: Couldn't initialize SDL2: %s\n", SDL_GetError());
            return -1;
        }

        SDL_SetHint("SDL_HINT_RENDER_SCALE_QUALITY", "0");
		
		sdl_window = SDL_CreateWindow(
			"Guvcview Video",                  // window title
			SDL_WINDOWPOS_UNDEFINED,           // initial x position
			SDL_WINDOWPOS_UNDEFINED,           // initial y position
			w,                               // width, in pixels
			h,                               // height, in pixels
			my_flags
		);

		if(sdl_window == NULL)
		{
			fprintf(stderr, "RENDER: (SDL2) Couldn't open window: %s\n", SDL_GetError());
			render_sdl2_clean();
            return -2;
		}

		int display_index = SDL_GetWindowDisplayIndex(sdl_window);

		int err = SDL_GetDesktopDisplayMode(display_index, &display_mode);
		if(!err)
		{
			if(verbosity > 0)
				printf("RENDER: video display %i ->  %dx%dpx @ %dhz\n",
					display_index,
					display_mode.w,
					display_mode.h,
					display_mode.refresh_rate);
		}
		else
			fprintf(stderr, "RENDER: Couldn't determine display mode for video display %i\n", display_index);

		if(w > display_mode.w)
			w = display_mode.w;
		if(h > display_mode.h)
			h = display_mode.h;

		if(verbosity > 0)
			printf("RENDER: setting window size to %ix%i\n", w, h);

		SDL_SetWindowSize(sdl_window, w, h);
		
    }

    if(verbosity > 2)
    {
		/* Allocate a renderer info struct*/
        SDL_RendererInfo *rend_info = (SDL_RendererInfo *) malloc(sizeof(SDL_RendererInfo));
        if (!rend_info)
        {
                fprintf(stderr, "RENDER: Couldn't allocate memory for the renderer info data structure\n");
                render_sdl2_clean();
                return -5;
        }
        /* Print the list of the available renderers*/
        printf("\nRENDER: Available SDL2 rendering drivers:\n");
        int i = 0;
        for (i = 0; i < SDL_GetNumRenderDrivers(); i++)
        {
            if (SDL_GetRenderDriverInfo(i, rend_info) < 0)
            {
                fprintf(stderr, " Couldn't get SDL2 render driver information: %s\n", SDL_GetError());
            }
            else
            {
                printf(" %2d: %s\n", i, rend_info->name);
                printf("    SDL_RENDERER_TARGETTEXTURE [%c]\n", (rend_info->flags & SDL_RENDERER_TARGETTEXTURE) ? 'X' : ' ');
                printf("    SDL_RENDERER_SOFTWARE      [%c]\n", (rend_info->flags & SDL_RENDERER_SOFTWARE) ? 'X' : ' ');
                printf("    SDL_RENDERER_ACCELERATED   [%c]\n", (rend_info->flags & SDL_RENDERER_ACCELERATED) ? 'X' : ' ');
                printf("    SDL_RENDERER_PRESENTVSYNC  [%c]\n", (rend_info->flags & SDL_RENDERER_PRESENTVSYNC) ? 'X' : ' ');
            }
        }

        free(rend_info);
	}

    main_renderer = SDL_CreateRenderer(sdl_window, -1,
		SDL_RENDERER_TARGETTEXTURE |
		SDL_RENDERER_PRESENTVSYNC  |
		SDL_RENDERER_ACCELERATED);

	if(main_renderer == NULL)
	{
		fprintf(stderr, "RENDER: (SDL2) Couldn't get a accelerated renderer: %s\n", SDL_GetError());
		fprintf(stderr, "RENDER: (SDL2) trying with a software renderer\n");

		main_renderer = SDL_CreateRenderer(sdl_window, -1,
		SDL_RENDERER_TARGETTEXTURE |
		SDL_RENDERER_SOFTWARE);


		if(main_renderer == NULL)
		{
			fprintf(stderr, "RENDER: (SDL2) Couldn't get a software renderer: %s\n", SDL_GetError());
			fprintf(stderr, "RENDER: (SDL2) giving up...\n");
			render_sdl2_clean();
			return -3;
		}
	}

	if(verbosity > 2)
    {
		/* Allocate a renderer info struct*/
        SDL_RendererInfo *rend_info = (SDL_RendererInfo *) malloc(sizeof(SDL_RendererInfo));
        if (!rend_info)
        {
                fprintf(stderr, "RENDER: Couldn't allocate memory for the renderer info data structure\n");
                render_sdl2_clean();
                return -5;
        }

		/* Print the name of the current rendering driver */
		if (SDL_GetRendererInfo(main_renderer, rend_info) < 0)
		{
			fprintf(stderr, "Couldn't get SDL2 rendering driver information: %s\n", SDL_GetError());
		}
		printf("RENDER: rendering driver in use: %s\n", rend_info->name);
		printf("    SDL_RENDERER_TARGETTEXTURE [%c]\n", (rend_info->flags & SDL_RENDERER_TARGETTEXTURE) ? 'X' : ' ');
		printf("    SDL_RENDERER_SOFTWARE      [%c]\n", (rend_info->flags & SDL_RENDERER_SOFTWARE) ? 'X' : ' ');
		printf("    SDL_RENDERER_ACCELERATED   [%c]\n", (rend_info->flags & SDL_RENDERER_ACCELERATED) ? 'X' : ' ');
		printf("    SDL_RENDERER_PRESENTVSYNC  [%c]\n", (rend_info->flags & SDL_RENDERER_PRESENTVSYNC) ? 'X' : ' ');

		free(rend_info);
	}

	SDL_RenderSetLogicalSize(main_renderer, width, height);
	SDL_SetRenderDrawBlendMode(main_renderer, SDL_BLENDMODE_NONE);
	

	SDL_ShowCursor(SDL_DISABLE);


	
    rending_texture = SDL_CreateTexture(main_renderer,
		// SDL_PIXELFORMAT_IYUV,  // yuv420p
		// SDL_PIXELFORMAT_RGB24,
		// SDL_PIXELFORMAT_ARGB32,
		// SDL_PIXELFORMAT_RGBX8888,
		SDL_PIXELFORMAT_RGBA32, // Seems to be the more faster
		// SDL_PIXELFORMAT_YUY2,
		// SDL_PIXELFORMAT_BGRA32,
		SDL_TEXTUREACCESS_STREAMING,
		width,
		height);

	if(rending_texture == NULL)
	{
		fprintf(stderr, "RENDER: (SDL2) Couldn't get a texture for rending: %s\n", SDL_GetError());
		render_sdl2_clean();
		return -4;
	}

	// sdl_surface = SDL_GetWindowSurface(sdl_window);

	run = 1;

    return 0;
}

void* render_loop() {
	int frames = 0;
	int tick = 0;
	int tick_acc = 0;
	int tick_diff = 0;
	int sleep_ms = 0;
	float fps = 0;

	uint8_t *tmp_frame = malloc(frame_width * frame_height * 4);
        uint32_t *output_ptrs = NULL;

	tjhandle tj = tjInitDecompress();

	int width = frame_width;
	int height = frame_height;

	int frame_size = width * height;

	unsigned char *srcPlanes[3];

        video_init2(frame_width, frame_height, sdl_init_flags);

        while (run) {
	    tick = SDL_GetTicks();

            if (frame_ptr && frame_width) {
        	output_ptrs = (uint32_t*) frame_ptr;
		
		srcPlanes[0] = output_ptrs[0];
		srcPlanes[1] = output_ptrs[1];
		srcPlanes[2] = output_ptrs[2];

		
		if (srcPlanes[0] && srcPlanes[1] && srcPlanes[2]) {
			tjDecodeYUVPlanes(tj, srcPlanes, NULL, TJSAMP_422, tmp_frame, width, width * 4, height, TJPF_RGBX, 0);
			// color_convert_common(srcPlanes[0], srcPlanes[1], srcPlanes[2], width, height, tmp_frame, 0);
		} else {
			// printf("Else ???\n");
		}

    		// use for RGBA
		// SDL_UpdateTexture(rending_texture, NULL, tmp_frame, frame_width * 4);
		SDL_UpdateTexture(rending_texture, NULL, tmp_frame, frame_width * 4);

		// why this is so slow??
                // SDL_UpdateTexture(rending_texture, NULL, srcPlanes[0], frame_width);
		
		// SDL_UpdateYUVTexture(rending_texture, NULL, yplane, frame_width, uplane, frame_width / 2, vplane, frame_width / 2); 
                // SDL_UpdateTexture(rending_texture, NULL, tmp_frame, frame_width * 2);
		SDL_RenderCopy(main_renderer, rending_texture, NULL, NULL);

                SDL_RenderPresent(main_renderer);
            }

	    //free(frame_ptr);
	    // frame_ptr = NULL;

	    frames++;
	    tick_diff = SDL_GetTicks() - tick;
	    sleep_ms = 16 - tick_diff;

	    if (sleep_ms < 0)
		    sleep_ms = 0;

	    usleep(sleep_ms * 1000);

	    tick_acc = tick_acc + tick_diff + sleep_ms;

	    if (tick_acc > 1000) {
		fps = frames / (tick_acc / 1000.0);
		printf("SDL FPS = %.2f \n", fps);
		frames = 0;
		tick_acc = 0;
	    }
        }
}

static int video_init(int width, int height, int flags) {
    frame_width = width;
    frame_height = height;
    sdl_init_flags = flags;

    pthread_create(&thread_id, NULL, render_loop, NULL);
    return 0;
}

/*
 * init sdl2 render
 * args:
 *    width - overlay width
 *    height - overlay height
 *    flags - window flags:
 *              0- none
 *              1- fullscreen
 *              2- maximized
 *
 * asserts:
 *
 * returns: error code (0 ok)
 */
 int init_render_sdl2(int width, int height, int flags)
 {
	int err = video_init(width, height, flags);

	if(err)
	{
		fprintf(stderr, "RENDER: Couldn't init the SDL2 rendering engine\n");
		return -1;
	}

	// assert(rending_texture != NULL);

	return 0;
 }

/*
 * render a frame
 * args:
 *   frame - pointer to frame data (yuyv format)
 *   width - frame width
 *   height - frame height
 *
 * asserts:
 *   poverlay is not nul
 *   frame is not null
 *
 * returns: error code
 */
int render_sdl2_frame(uint8_t *frame, int width, int height)
{
	/*asserts*/
	// assert(rending_texture != NULL);
	assert(frame != NULL);
	
	// uint8_t *copyframe = malloc(width * height * 4);
	// memcpy(copyframe, frame, width * height * 4);

	frame_ptr = frame; //copyframe;
	frame_width = width;
	frame_height = height;

	// SDL_SetRenderDrawColor(main_renderer, 0, 0, 0, 255); /*black*/
	// SDL_RenderClear(main_renderer);

	/* since data is continuous we can use SDL_UpdateTexture
	 * instead of SDL_UpdateYUVTexture.
	 * no need to use SDL_Lock/UnlockTexture (it doesn't seem faster)
	 */
	// SDL_UpdateTexture(rending_texture, NULL, frame, width * 4);

	// SDL_RenderCopy(main_renderer, rending_texture, NULL, NULL);

	// SDL_RenderPresent(main_renderer);

	return 0;
}

/*
 * set sdl2 render caption
 * args:
 *   caption - string with render window caption
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void set_render_sdl2_caption(const char* caption)
{
	SDL_SetWindowTitle(sdl_window, caption);
}

/*
 * dispatch sdl2 render events
 * args:
 *   none
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void render_sdl2_dispatch_events()
{

	SDL_Event event;

	while( SDL_PollEvent(&event) )
	{
		if(event.type==SDL_KEYDOWN)
		{
			switch( event.key.keysym.sym )
            {
				case SDLK_ESCAPE:
					render_call_event_callback(EV_QUIT);
					break;

				case SDLK_UP:
					render_call_event_callback(EV_KEY_UP);
					break;

				case SDLK_DOWN:
					render_call_event_callback(EV_KEY_DOWN);
					break;

				case SDLK_RIGHT:
					render_call_event_callback(EV_KEY_RIGHT);
					break;

				case SDLK_LEFT:
					render_call_event_callback(EV_KEY_LEFT);
					break;

				case SDLK_SPACE:
					render_call_event_callback(EV_KEY_SPACE);
					break;

				case SDLK_i:
					render_call_event_callback(EV_KEY_I);
					break;

				case SDLK_v:
					render_call_event_callback(EV_KEY_V);
					break;

				default:
					break;

			}

			//switch( event.key.keysym.scancode )
			//{
			//	case 220:
			//		break;
			//	default:
			//		break;
			//}
		}

		if(event.type==SDL_QUIT)
		{
			if(verbosity > 0)
				printf("RENDER: (event) quit\n");
			render_call_event_callback(EV_QUIT);
		}
	}
}
/*
 * clean sdl2 render data
 * args:
 *   none
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void render_sdl2_clean()
{
	run = 0;
	
	pthread_join(thread_id, NULL);

	if(rending_texture)
		SDL_DestroyTexture(rending_texture);

	rending_texture = NULL;

	if(main_renderer)
		SDL_DestroyRenderer(main_renderer);

	main_renderer = NULL;

	if(sdl_window)
		SDL_DestroyWindow(sdl_window);

	sdl_window = NULL;

	SDL_Quit();
}

