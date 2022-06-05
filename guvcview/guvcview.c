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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <errno.h>

#include "gview.h"
#include "gviewv4l2core.h"
#include "core_time.h"

#include "../config.h"
#include "video_capture.h"
#include "options.h"
#include "config.h"
#include "core_io.h"

int persist_run = 0;
int debug_level = 0;

static __THREAD_TYPE capture_thread;

__MUTEX_TYPE capture_mutex = __STATIC_MUTEX_INIT;
__COND_TYPE capture_cond;

char *get_profile_name() 
{
	return strdup("default.gpfl");
}

char *get_profile_path() {
	return strdup(getenv("HOME"));
}

/*
 * signal callback
 * args:
 *    signum - signal number
 *
 * return: none
 */
void signal_callback_handler(int signum)
{
	printf("GUVCVIEW Caught signal %d\n", signum);

	switch(signum)
	{
		case SIGINT:
			/* Terminate program */
			persist_run = 0;
			quit_callback(NULL);
			break;

	}
}

int main(int argc, char *argv[])
{
	/*check stack size*/
	const rlim_t kStackSize = 128L * 1024L * 1024L;   /* min stack size = 128 Mb*/
    struct rlimit rl;
    int result;

    persist_run = 1;

    result = getrlimit(RLIMIT_STACK, &rl);
    if (result == 0)
    {
        if (rl.rlim_cur < kStackSize)
        {
            rl.rlim_cur = kStackSize;
            result = setrlimit(RLIMIT_STACK, &rl);
            if (result != 0)
            {
                fprintf(stderr, "GUVCVIEW: setrlimit returned result = %d\n", result);
            }
        }
    }
	
	// Register signal and signal handler
	signal(SIGINT,  signal_callback_handler);
	signal(SIGUSR1, signal_callback_handler);
	signal(SIGUSR2, signal_callback_handler);
	
	/*localization*/
	char* lc_all = setlocale (LC_ALL, "");
	char* lc_dir = bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	char* txtdom = textdomain (GETTEXT_PACKAGE);

	/*parse command line options*/
	if(options_parse(argc, argv))
		return 0;
	
	printf("GUVCVIEW: version %s\n", VERSION);

	/*get command line options*/
	options_t *my_options = options_get();

	char *config_path = smart_cat(getenv("HOME"), '/', ".config/guvcview2");
	mkdir(config_path, 0777);

	char *device_name = get_file_basename(my_options->device);

	char *config_file = smart_cat(config_path, '/', device_name);

	/*clean strings*/
	free(config_path);
	free(device_name);

	/*load config data*/
	config_load(config_file);

	/*update config with options*/
	config_update(my_options);

	/*get config data*/
	config_t *my_config = config_get();

	debug_level = my_options->verbosity;
	
	if (debug_level > 1) printf("GUVCVIEW: language catalog=> dir:%s type:%s cat:%s.mo\n",
		lc_dir, lc_all, txtdom);

	v4l2core_set_verbosity(debug_level);

	v4l2core_start();

	while (persist_run) {
		/*set the v4l2core device (redefines language catalog)*/
		v4l2_dev_t *vd = create_v4l2_device_handler(my_options->device);
		if(!vd)
		{
			char message[100];
			snprintf(message, 100, "no video device (%s) found", my_options->device);
			options_clean();
			sleep(5);
			continue;
		}
		
		if(my_options->disable_libv4l2)
			v4l2core_disable_libv4l2(vd);

		/*select capture method*/
		if(strcasecmp(my_config->capture, "read") == 0)
			v4l2core_set_capture_method(vd, IO_READ);
		else
			v4l2core_set_capture_method(vd, IO_MMAP);

		/*set software autofocus sort method*/
		v4l2core_soft_autofocus_set_sort(AUTOF_SORT_INSERT);

		/*set the intended fps*/
		v4l2core_define_fps(vd, my_config->fps_num,my_config->fps_denom);

		/*select video codec*/
		if(debug_level > 1)
			printf("GUVCVIEW: setting video codec to '%s'\n", my_config->video_codec);
			

		/*check if need to load a profile*/
		if(my_options->prof_filename)
			v4l2core_load_control_profile(vd, my_options->prof_filename);

		/*set the profile file*/
		my_config->profile_name = strdup(get_profile_name());
		my_config->profile_path = strdup(get_profile_path());

		/*
		 * prepare format:
		 *   doing this inside the capture thread may create a race
		 *   condition with gui_attach, as it requires the current
		 *   format to be set
		 */
		v4l2core_prepare_new_format(vd, my_config->format);
		/*prepare resolution*/
		v4l2core_prepare_new_resolution(vd, my_config->width, my_config->height);
		/*try to set the video stream format on the device*/
		int ret = v4l2core_update_current_format(vd);

		if(ret != E_OK)
		{
			fprintf(stderr, "GUCVIEW: could not set the defined stream format\n");
			fprintf(stderr, "GUCVIEW: trying first listed stream format\n");

			v4l2core_prepare_valid_format(vd);
			v4l2core_prepare_valid_resolution(vd);
			ret = v4l2core_update_current_format(vd);

			if(ret != E_OK)
			{
				fprintf(stderr, "GUCVIEW: also could not set the first listed stream format\n");
				fprintf(stderr, "GUVCVIEW: Video capture failed\n");
			}
		}

		if(ret == E_OK)
		{
			__INIT_COND(&capture_cond);
			__LOCK_MUTEX(&capture_mutex);

			capture_loop_data_t cl_data;
			cl_data.options = (void *) my_options;
			cl_data.config = (void *) my_config;

			ret = __THREAD_CREATE(&capture_thread, capture_loop, (void *) &cl_data);

			if(ret)
			{
				fprintf(stderr, "GUVCVIEW: Video thread creation failed\n");
			}
			else if(debug_level > 2)
				printf("GUVCVIEW: created capture thread with tid: %u\n", (unsigned int) capture_thread);

			struct timespec now;
			clock_gettime(CLOCK_REALTIME, &now);
			now.tv_sec += 5; /*wait at most 5 seconds for capture_cond*/
			ret = __COND_TIMED_WAIT(&capture_cond,&capture_mutex, &now);
			__UNLOCK_MUTEX(&capture_mutex);
			__CLOSE_COND(&capture_cond);

			if(ret == ETIMEDOUT)
				fprintf(stderr, "GUVCVIEW: capture_cond wait timedout (5 sec)\n");
			else if (ret != 0)
				fprintf(stderr, "GUVCVIEW: capture_cond wait unknown error: %i\n", ret);
		}

		if(debug_level > 2)
			printf("GUVCVIEW: joining capture thread\n");

		if(!my_options->control_panel)
			__THREAD_JOIN(capture_thread, (void **)&ret);

		if (ret == ENODEV) {
			printf("GUVCVIEW: no such device, will retry until device is available\n");
			persist_run = 1;
			ret = 0;
		} else {
			persist_run = 0;
		}
	}


    /*save config before cleaning the options*/
	config_save(config_file);

	if(config_file)
		free(config_file);

	config_clean();
	options_clean();

	v4l2core_stop();

	if(debug_level > 0)
		printf("GUVCVIEW: good bye\n");

	return 0;
}
