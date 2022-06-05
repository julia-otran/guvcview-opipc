/*******************************************************************************#
#           guvcview              http://guvcview.sourceforge.net               #
#                                                                               #
#           Paulo Assis <pj.assis@gmail.com>                                    #
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

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <math.h>
/* support for internationalization - i18n */
#include <locale.h>
#include <libintl.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>

#include "gviewv4l2core.h"
#include "gview.h"
#include "video_capture.h"
#include "options.h"
#include "config.h"
#include "core_io.h"
#include "../config.h"
#include "display.h"
#include "controls.h"

/*flags*/
extern int debug_level;

extern __MUTEX_TYPE capture_mutex;
extern __COND_TYPE capture_cond;

static int quit = 0; /*terminate flag*/
static int restart = 0; /*restart flag*/

/*continues focus*/
static int do_soft_autofocus = 0;
/*single time focus (can happen during continues focus)*/
static int do_soft_focus = 0;

/*pointer to v4l2 device handler*/
static v4l2_dev_t *my_vd = NULL;

/*
 * set software autofocus flag
 * args:
 *    value - flag value
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void set_soft_autofocus(int value)
{
	do_soft_autofocus = value;
}

/*
 * set software focus flag
 * args:
 *    value - flag value
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void set_soft_focus(int value)
{
	v4l2core_soft_autofocus_set_focus();

	do_soft_focus = value;
}
/*
 * request format update
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void request_format_update()
{
	restart = 1;
}

/*
 * quit callback
 * args:
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int quit_callback(void *data)
{
	quit = 1;

	return 0;
}

/************ RENDER callbacks *******************/
/*
 * key I pressed callback
 * args:
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int key_I_callback(void *data)
{
	return 0;
}

/*
 * key V pressed callback
 * args:
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int key_V_callback(void *data)
{

	return 0;
}

/*
 * key DOWN pressed callback
 * args:
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int key_DOWN_callback(void *data)
{
	if(v4l2core_has_pantilt_id(my_vd))
    {
		int id = V4L2_CID_TILT_RELATIVE;
		int value = v4l2core_get_tilt_step(my_vd);

		v4l2_ctrl_t *control = v4l2core_get_control_by_id(my_vd, id);

		if(control)
		{
			control->value =  value;

			if(v4l2core_set_control_value_by_id(my_vd, id))
				fprintf(stderr, "GUVCVIEW: error setting pan/tilt value\n");

			return 0;
		}
	}

	return -1;
}

/*
 * key UP pressed callback
 * args:
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int key_UP_callback(void *data)
{
	if(v4l2core_has_pantilt_id(my_vd))
    {
		int id = V4L2_CID_TILT_RELATIVE;
		int value = - v4l2core_get_tilt_step(my_vd);

		v4l2_ctrl_t *control = v4l2core_get_control_by_id(my_vd, id);

		if(control)
		{
			control->value =  value;

			if(v4l2core_set_control_value_by_id(my_vd, id))
				fprintf(stderr, "GUVCVIEW: error setting pan/tilt value\n");

			return 0;
		}
	}

	return -1;
}

/*
 * key LEFT pressed callback
 * args:
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int key_LEFT_callback(void *data)
{
	if(v4l2core_has_pantilt_id(my_vd))
    {
		int id = V4L2_CID_PAN_RELATIVE;
		int value = v4l2core_get_pan_step(my_vd);

		v4l2_ctrl_t *control = v4l2core_get_control_by_id(my_vd, id);

		if(control)
		{
			control->value =  value;

			if(v4l2core_set_control_value_by_id(my_vd, id))
				fprintf(stderr, "GUVCVIEW: error setting pan/tilt value\n");

			return 0;
		}
	}

	return -1;
}

/*
 * key RIGHT pressed callback
 * args:
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int key_RIGHT_callback(void *data)
{
	if(v4l2core_has_pantilt_id(my_vd))
    {
		int id = V4L2_CID_PAN_RELATIVE;
		int value = - v4l2core_get_pan_step(my_vd);

		v4l2_ctrl_t *control = v4l2core_get_control_by_id(my_vd, id);

		if(control)
		{
			control->value =  value;

			if(v4l2core_set_control_value_by_id(my_vd, id))
				fprintf(stderr, "GUVCVIEW: error setting pan/tilt value\n");

			return 0;
		}
	}

	return -1;
}

/*
 * create a v4l2 device handler
 * args:
 *    device - device name
 *
 * asserts:
 *    none
 *
 * returns: pointer to v4l2 device handler (or null on error)
 */
v4l2_dev_t *create_v4l2_device_handler(const char *device)
{
	my_vd = v4l2core_init_dev(device);
	
	return my_vd;
}

/*
 * close the v4l2 device handler
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void close_v4l2_device_handler()
{
	/*closes the video device*/
	v4l2core_close_dev(my_vd);

	my_vd = NULL;
}

/*
 * get the v4l2 device handler
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: pointer to v4l2 device handler
 */
v4l2_dev_t *get_v4l2_device_handler()
{
	return my_vd;
}

/*
 * capture loop (should run in a separate thread)
 * args:
 *    data - pointer to user data (options data)
 *
 * asserts:
 *    none
 *
 * returns: pointer to return code
 */
void *capture_loop(void *data)
{
	__LOCK_MUTEX(&capture_mutex);

	/*reset quit flag*/
	quit = 0;
	
	if(debug_level > 1)
		printf("GUVCVIEW: capture thread (tid: %u)\n", 
			(unsigned int) syscall (SYS_gettid));

	int ret = 0;
	int err = 0;
	
	v4l2core_start_stream(my_vd);

	v4l2_frame_buff_t *frame = NULL; //pointer to frame buffer

	int count = 0;

	__COND_SIGNAL(&capture_cond);
	__UNLOCK_MUTEX(&capture_mutex);

	while(!quit)
	{
		if(restart)
		{
			restart = 0; /*reset*/
			v4l2core_stop_stream(my_vd);

			v4l2core_clean_buffers(my_vd);

			/*try new format (values prepared by the request callback)*/
			ret = v4l2core_update_current_format(my_vd);
			/*try to set the video stream format on the device*/
			if(ret != E_OK)
			{
				fprintf(stderr, "GUCVIEW: could not set the defined stream format\n");
				fprintf(stderr, "GUCVIEW: trying first listed stream format\n");

				v4l2core_prepare_valid_format(my_vd);
				v4l2core_prepare_valid_resolution(my_vd);
				ret = v4l2core_update_current_format(my_vd);

				if(ret != E_OK)
				{
					fprintf(stderr, "GUCVIEW: also could not set the first listed stream format\n");

					return ((void *) -1);
				}
			}

			if(debug_level > 0)
				printf("GUVCVIEW: reset to pixelformat=%x width=%i and height=%i\n",
					v4l2core_get_requested_frame_format(my_vd),
					v4l2core_get_frame_width(my_vd),
					v4l2core_get_frame_height(my_vd));

			v4l2core_start_stream(my_vd);

		}

		update_controls(my_vd);
		frame = v4l2core_get_decoded_frame(my_vd, &err);

		if( frame != NULL)
		{
			/*run software autofocus (must be called after frame was grabbed and decoded)*/
			if(do_soft_autofocus || do_soft_focus)
				do_soft_focus = v4l2core_soft_autofocus_run(my_vd, frame);

			/* finally render the frame */
			// snprintf(render_caption, 29, "Guvcview  (%2.2f fps)", 
			if (count > 50) {
			  printf("FPS = %2.2f\n", v4l2core_get_realfps(my_vd));
			  count = 0;
			}

			count++;

			/*we are done with the frame buffer release it*/
			v4l2core_release_frame(my_vd, frame);
		}

		if (err == ENODEV) {
			v4l2core_stop_stream(my_vd);
			v4l2core_close_dev(my_vd);
			return ((void *)err);
		}
	}

	v4l2core_stop_stream(my_vd);
	v4l2core_close_dev(my_vd);

	return ((void *) 0);
}

