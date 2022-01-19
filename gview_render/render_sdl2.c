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

#include <glfw3.h>
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <turbojpeg.h>
#include <arm_neon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gview.h"
#include "gviewrender.h"
#include "render.h"
#include "render_sdl2.h"
#include "../config.h"

extern int verbosity;

pthread_t thread_id;

uint8_t *frame_ptr;
int frame_width;
int frame_height;
int run = 1;
int should_close = 0;

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

static GLFWwindow *window;

static int video_init2(int width, int height)
{

    glfwInit();

    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);

    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

    window = glfwCreateWindow(width, height, "GUVCview", monitor, NULL);

    should_close = 0;
    run = 1;
    return 0;
}

void* render_loop() {
	int frames = 0;
	double ticks_prev = 0;
	double ticks_current = 0;
	double ticks_diff = 0;
	double fps = 0;

	int width = frame_width;
        int height = frame_height;
	int frame_size = width * height;

	uint8_t *tmp_frame = malloc(frame_size * 4);
        uint32_t *output_ptrs = NULL;

	tjhandle tj = tjInitDecompress();

	unsigned char *y_plane;
	unsigned char *u_plane;
	unsigned char *v_plane;
	unsigned char *planes[3];

        video_init2(frame_width, frame_height);

	glfwMakeContextCurrent(window);

	glfwSwapInterval(1);

	glViewport(0, 0, width, height);

	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);

	unsigned int texture;

	glGenTextures(1, &texture);

	ticks_prev = glfwGetTime();

        while (run && !should_close) {
	    glActiveTexture(GL_TEXTURE0);
	    glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

            if (frame_ptr && frame_width) {
        	output_ptrs = (uint32_t*) frame_ptr;
		
		y_plane = (unsigned char*) output_ptrs[0];
		u_plane = (unsigned char*) output_ptrs[1];
		v_plane = (unsigned char*) output_ptrs[2];

		planes[0] = y_plane;
		planes[1] = u_plane;
		planes[2] = v_plane;
		
		if (y_plane && u_plane && v_plane) {
			tjDecodeYUVPlanes(tj, planes, NULL, TJSAMP_422, tmp_frame, width, width * 4, height, TJPF_RGBA, 0);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tmp_frame);

		}
            }

	    glBegin(GL_QUADS);
		glTexCoord2f(0.0, 1.0);
		glVertex2f(-1.0, -1.0);

		glTexCoord2f(1.0, 1.0);
		glVertex2f(1.0, -1.0);

		glTexCoord2f(1.0, 0.0);
		glVertex2f(1.0, 1.0);

		glTexCoord2f(0.0, 0.0);
		glVertex2f(-1.0, 1.0);
	    glEnd();

	    glfwSwapBuffers(window);
	    glfwPollEvents();
	    should_close = glfwWindowShouldClose(window);

	    frames++;
	    ticks_current = glfwGetTime();
	    ticks_diff = ticks_current - ticks_prev;

	    if (ticks_diff > 1.0) {
		    fps = frames / ticks_diff;
		    frames = 0;
		    ticks_prev = ticks_current;

		    printf("OpenGL FPS: %2.2f\n", fps);
	    }

        }

	glfwDestroyWindow(window);
	glfwTerminate();
}

static int video_init(int width, int height) {
    frame_width = width;
    frame_height = height;

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
	int err = video_init(width, height);

	if(err)
	{
		fprintf(stderr, "RENDER: Couldn't init the SDL2 rendering engine\n");
		return -1;
	}

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
	assert(frame != NULL);
	
	frame_ptr = frame; //copyframe;
	frame_width = width;
	frame_height = height;

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

		if(should_close)
		{
			render_call_event_callback(EV_QUIT);
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
}

