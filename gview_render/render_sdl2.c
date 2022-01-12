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

pthread_t thread_id;

uint8_t *frame_ptr;
int frame_width;
int frame_height;
int run = 1;

int sdl_init_flags;

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
		//SDL_PIXELFORMAT_RGBX8888,
		SDL_PIXELFORMAT_RGBA32,
		SDL_TEXTUREACCESS_STREAMING,
		width,
		height);

	if(rending_texture == NULL)
	{
		fprintf(stderr, "RENDER: (SDL2) Couldn't get a texture for rending: %s\n", SDL_GetError());
		render_sdl2_clean();
		return -4;
	}

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

        video_init2(frame_width, frame_height, sdl_init_flags);

        while (run) {
	    tick = SDL_GetTicks();

            if (frame_ptr && frame_width) {
                SDL_UpdateTexture(rending_texture, NULL, frame_ptr, frame_width * 4);
                SDL_RenderCopy(main_renderer, rending_texture, NULL, NULL);

                SDL_RenderPresent(main_renderer);
            }

	    free(frame_ptr);
	    frame_ptr = NULL;

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
	
	if (frame_ptr != NULL) {
		return 0;
	}

	uint8_t *copyframe = malloc(width * height * 4);
	memcpy(copyframe, frame, width * height * 4);

	frame_ptr = copyframe;
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

