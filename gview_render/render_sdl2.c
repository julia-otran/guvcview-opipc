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


  memcpy(buffer, pY, width * height);

  int x, y, p, i;

  int halfW = width / 2;

  unsigned char *out = buffer + width * height;

  for (y=0; y < height; y+=2)
  for (x=0; x < halfW; x += 16) {
	  p = (y * halfW) + x;

	  uint8x16_t load1 = vld1q_u8(pU + p);
	  uint8x16_t load2 = vld1q_u8(pU + p + halfW);

	  uint16x8_t load11 = vmovl_u8(vget_low_u8(load1));
	  uint16x8_t load12 = vmovl_u8(vget_high_u8(load1));
	  uint16x8_t load21 = vmovl_u8(vget_low_u8(load2));
	  uint16x8_t load22 = vmovl_u8(vget_high_u8(load2));

	  uint8x8_t avg1 = vmovn_u16(vshrq_n_u16(vaddq_u16(load11, load21), 1));
	  uint8x8_t avg2 = vmovn_u16(vshrq_n_u16(vaddq_u16(load12, load22), 1));

	  load1 = vcombine_u8(avg1, avg2);
	  vst1q_u8(out, load1);
	  out += 16; 
  }

  for (y=0; y < height; y+=2)
  for (x=0; x < halfW; x += 16) {
          p = (y * halfW) + x;

          uint8x16_t load1 = vld1q_u8(pV + p);
          uint8x16_t load2 = vld1q_u8(pV + p + width / 2);

          uint16x8_t load11 = vmovl_u8(vget_low_u8(load1));
          uint16x8_t load12 = vmovl_u8(vget_high_u8(load1));
          uint16x8_t load21 = vmovl_u8(vget_low_u8(load2));
          uint16x8_t load22 = vmovl_u8(vget_high_u8(load2));

          uint8x8_t avg1 = vmovn_u16(vshrq_n_u16(vaddq_u16(load11, load21), 1));
          uint8x8_t avg2 = vmovn_u16(vshrq_n_u16(vaddq_u16(load12, load22), 1));

          load1 = vcombine_u8(avg1, avg2);
          vst1q_u8(out, load1);
          out += 16;
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
		SDL_PIXELFORMAT_IYUV,  // yuv420p
		// SDL_PIXELFORMAT_RGB24,
		// SDL_PIXELFORMAT_ARGB32,
		// SDL_PIXELFORMAT_RGBX8888,
		// SDL_PIXELFORMAT_RGBA32, // Seems to be the more faster
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

	int width = frame_width;
        int height = frame_height;
	int frame_size = width * height;

	uint8_t *tmp_frame = malloc(frame_size * 2);
        uint32_t *output_ptrs = NULL;

	tjhandle tj = tjInitDecompress();

	unsigned char *y_plane;
	unsigned char *u_plane;
	unsigned char *v_plane;

        video_init2(frame_width, frame_height, sdl_init_flags);

        while (run) {
	    tick = SDL_GetTicks();

            if (frame_ptr && frame_width) {
        	output_ptrs = (uint32_t*) frame_ptr;
		
		y_plane = (unsigned char*) output_ptrs[0];
		u_plane = (unsigned char*) output_ptrs[1];
		v_plane = (unsigned char*) output_ptrs[2];

		
		if (y_plane && u_plane && v_plane) {
			// tjDecodeYUVPlanes(tj, srcPlanes, NULL, TJSAMP_422, tmp_frame, width, width * 4, height, TJPF_RGBX, 0);
			color_convert_common(y_plane, u_plane, v_plane, width, height, tmp_frame, 0);
		}

    		// use for RGBA
		// SDL_UpdateTexture(rending_texture, NULL, tmp_frame, frame_width * 4);

		// why this is so slow??
                // SDL_UpdateTexture(rending_texture, NULL, srcPlanes[0], frame_width);
		
		// SDL_UpdateYUVTexture(rending_texture, NULL, yplane, frame_width, uplane, frame_width / 2, vplane, frame_width / 2); 
                SDL_UpdateTexture(rending_texture, NULL, tmp_frame, frame_width);
		// SDL_UpdateYUVTexture(rending_texture, NULL, y_plane, frame_width, u_plane, width / 2, v_plane, width / 2); 
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

