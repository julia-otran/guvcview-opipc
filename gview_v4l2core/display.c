#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <drm/sun4i_drm.h>

#include "display.h"

#define PAGE_SIZE sysconf(_SC_PAGESIZE)

#define VIRT_TO_PHYS (0xc0000000)

#define Y_VALUE 0
#define U_VALUE 128
#define V_VALUE 128

typedef uint8_t buffer_t[3];

static uint32_t src_width;
static uint32_t src_height;

static uint32_t data_offsets[2];

static uint32_t buf_id;
static int drm_fd;
static int buf_fd;

static uint32_t buf_id2;
static int drm_fd2;
static int buf_fd2;

static uint32_t buf_id3;
static int drm_fd3;
static int buf_fd3;

static void *buffer_map;
static void *buffer_map2;
static void *buffer_map3;

static uint32_t buffer_size;

static struct drm_sun4i_gem_create data;
static struct drm_sun4i_gem_create data2;
static struct drm_sun4i_gem_create data3;

static drmModePlane *old_plane;
static drmModePlane *new_plane;
static drmModeCrtc *crtc;

static uint32_t drm_mode_pixel_format;

static pthread_mutex_t current_values_lock;
static pthread_cond_t available_buffer_cond;
static pthread_cond_t display_buffer_cond;

static buffer_t current_display_buffer;
static buffer_t current_available_buffer;

static uint8_t run_video_update;
static pthread_t display_thread;

void forward(buffer_t arr) {
	arr[0] = arr[1];
	arr[1] = arr[2];
	arr[2] = 0;
}

void put(buffer_t arr, uint8_t data) {
	if (arr[0] == 0) {
		arr[0] = data;
	} else if (arr[1] == 0) {
		arr[1] = data;
	} else if (arr[2] == 0) {
		arr[2] = data;
	} 
}

void* display_thread_loop(void *data) {
	int result;
	uint8_t display_buffer = 0;
	uint8_t prev_display_buffer = 0;
	uint8_t should_draw = 0;

	int buf_ids[4] = { 0, buf_id, buf_id2, buf_id3 };

	while (run_video_update) {
		pthread_mutex_lock(&current_values_lock);

		if (!current_display_buffer[0]) {
			pthread_cond_wait(&display_buffer_cond, &current_values_lock);
		}

		if (current_display_buffer[0]) {
			should_draw = 1;
			prev_display_buffer = display_buffer;
			display_buffer = current_display_buffer[0];
			forward(&current_display_buffer);

		} else {
			should_draw = 0;
		}

		pthread_mutex_unlock(&current_values_lock);

		if (should_draw) {
 			result = drmModeSetPlane(drm_fd, new_plane->plane_id, crtc->crtc_id, buf_ids[display_buffer], 0, crtc->x, crtc->y, crtc->width, crtc->height, 0, 0, src_width, src_height);

			if (result) {
				printf("Setting plane failed %i\n", result);
			}
		}

		if (prev_display_buffer) {
			pthread_mutex_lock(&current_values_lock);

			put(&current_available_buffer, prev_display_buffer);

			pthread_cond_signal(&available_buffer_cond);

			pthread_mutex_unlock(&current_values_lock);

			prev_display_buffer = 0;
		}
		
	}
}

int get_buffer_number() {
	uint8_t write_buffer = 0;

	pthread_mutex_lock(&current_values_lock);

	if (current_available_buffer[0]) {
		write_buffer = current_available_buffer[0];
		forward(&current_available_buffer);
	} else if (current_display_buffer[0]) {
		printf("Waiting next frame. Is draw thread too slow?\n");
		pthread_cond_wait(&available_buffer_cond, &current_values_lock);

		write_buffer = current_available_buffer[0];
		forward(&current_available_buffer);
	} else {
		printf("Failed to dequeue available buffer\n");
		write_buffer = 0;
	}

	pthread_mutex_unlock(&current_values_lock);

	return write_buffer;
	
}

void put_buffer(uint8_t buffer_number) {
	pthread_mutex_lock(&current_values_lock);

	put(&current_display_buffer, buffer_number);
	
	pthread_cond_signal(&display_buffer_cond);

	pthread_mutex_unlock(&current_values_lock);
}

void start_drm() {
	int err = 0;
	int i = 0;
	int j = 0;

	printf("Starting display\n");

	drm_fd = drmOpen("sun4i-drm", NULL);

	drmVersion *ver = drmGetVersion(drm_fd);
	printf("driver name: %s\n", ver->name);

	printf("drm_fd %x\n", drm_fd);

	err = drmSetMaster(drm_fd);

	if (err) {
		printf("drm set master failed! %i\n", err);
	}

	drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

        drmModePlaneRes *plane_res = NULL;
        drmModePlane *plane = NULL;

        plane_res = drmModeGetPlaneResources(drm_fd);

        if (plane_res) {
                for (i = 0; i < plane_res->count_planes; i++) {
                        plane = drmModeGetPlane(drm_fd, plane_res->planes[i]);
                        if (plane && plane->fb_id) {
                                break;
                        } else if (plane) {
                                drmModeFreePlane(plane);
				plane = NULL;
                        }
                }
        } else {
                printf("cannot find plane_res\n");
		fflush(stdout);
        }

        if (plane) {
                old_plane = plane;
        } else {
                old_plane = NULL;
		printf("Failed to find current plane\n");
		fflush(stdout);
        }

	drmModeRes *resources = drmModeGetResources(drm_fd);

	if (plane) {
		crtc = drmModeGetCrtc(drm_fd, plane->crtc_id);
	} else {
		for (i = 0; i < resources->count_crtcs; i++) {
			crtc = drmModeGetCrtc(drm_fd, resources->crtcs[i]);
			if (crtc && crtc->buffer_id) {
				break;
			} else {
				drmModeFreeCrtc(crtc);
				crtc = NULL;
			}
		}
	}

	drmModeFreeResources(resources);

	if (!crtc) {
		printf("Failed to find CRTC\n");
		fflush(stdout);
	}

	drmModeFreePlaneResources(plane_res);

	// Set DRM Mode
	if (old_plane) {
		err = drmModeSetPlane(drm_fd, old_plane->plane_id, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	}
}

void find_new_plane() {
	int i, j;

        drmModePlaneRes *plane_res = NULL;
	drmModePlane *plane = NULL;

        plane_res = drmModeGetPlaneResources(drm_fd);

	new_plane = NULL;

	for (i = 0; i < plane_res->count_planes; i++) {
		plane = drmModeGetPlane(drm_fd, plane_res->planes[i]);

		if (plane) {
			for (j = 0; j < plane->count_formats; j++) {
				if (plane->formats[j] == drm_mode_pixel_format) {
					new_plane = plane;
				}
			}


			if (new_plane) {
				break;
			} else {
				drmModeFreePlane(plane);
			}
		}
	}

	drmModeFreePlaneResources(plane_res);

	if (!new_plane) {
		printf("Failed to find plane with valid format\n");
		fflush(stdout);
		return;
	}

}

void init_display(int width, int height, int format) {
	printf("Init buffers\n");

	uint8_t subsampling_divisor;
	uint32_t chroma_pitches_divisor;

	if (format == 0x22) {
		drm_mode_pixel_format = DRM_FORMAT_YUV420;
		subsampling_divisor = 4;
		chroma_pitches_divisor = 2;
		printf("Using YUV420 format\n");
	} else if (format == 0x21) {
		drm_mode_pixel_format = DRM_FORMAT_YUV422;
		subsampling_divisor = 2;
		chroma_pitches_divisor = 2;
		printf("Using YUV422 format\n");
	} else if (format == 0x11) {
		// TODO: I think the cedar may will output 422 format for 444 subsampling.
		// However, i dont have a device that outputs 444 to test.
		// If thats the case, adjust vars below to match 0x21 format.
		drm_mode_pixel_format = DRM_FORMAT_YUV444;
		subsampling_divisor = 1;
		chroma_pitches_divisor = 1;
		printf("MJPEG YUV444 is not tested! may need adjusts.\n");
		fflush(stdout);
	} else {
		// I don't know if GPU supports 0x12 (vertical subsampling only)
		printf("MJPEG YUV format not supported %x\n", format);
	}
	
	find_new_plane();

	// Calc buffer size and offsets
	uint32_t w_aligned = (width + 32) & ~32;
	uint32_t h_aligned = (height + 32) & ~32;

	uint32_t size_page_aligned = ((w_aligned * h_aligned) + PAGE_SIZE) & ~PAGE_SIZE;
	
	uint32_t u_offset = size_page_aligned;
	uint32_t u_size = ((size_page_aligned / subsampling_divisor) + PAGE_SIZE) & ~PAGE_SIZE;
	
	uint32_t v_offset = u_offset + u_size;
	uint32_t v_size = u_size;
	
	uint32_t total_size = v_offset + v_size;

	buffer_size = (total_size + PAGE_SIZE) & ~PAGE_SIZE;
	data_offsets[0] = u_offset;
	data_offsets[1] = v_offset;

	int err, i;

	pthread_mutex_init(&current_values_lock, NULL);
	pthread_cond_init(&available_buffer_cond, NULL);

	current_display_buffer[0] = 1;
	current_display_buffer[1] = 0;
	current_display_buffer[2] = 0;

	current_available_buffer[0] = 2;
	current_available_buffer[1] = 3;
	current_available_buffer[2] = 0;

	src_width = width << 16;
	src_height = height << 16;


	printf("Calculate buffer size and offsets\n");
	fflush(stdout);

	// Create and add buffer 1

	data.size = buffer_size;
	data.flags = 0;
	data.handle = 0;

	err = drmIoctl(drm_fd, DRM_IOCTL_SUN4I_GEM_CREATE, &data);

	if (err) {
		printf("Failed to create 1st GEM. %i\n", err);
	}

        const uint32_t bo_handles[4] = { data.handle, data.handle, data.handle, 0 };
        const uint32_t pitches[4] = { width, (width / chroma_pitches_divisor), (width / chroma_pitches_divisor), 0 };
        const uint32_t offsets[4] = { 0, u_offset, v_offset, 0 };

	buf_id = 0;

        err = drmModeAddFB2(drm_fd, width, height, drm_mode_pixel_format, bo_handles, pitches, offsets, &buf_id, 0);

	if (err) {
		printf("Failed to add 1st GEM. %i\n", err);
		fflush(stdout);
	}

	// Map buffer 1

	buf_fd = 0;

        err = drmPrimeHandleToFD(drm_fd, data.handle, DRM_RDWR, &buf_fd);

        buffer_map = mmap(0, buffer_size, PROT_WRITE, MAP_SHARED, buf_fd, 0);

	if (buffer_map) {
		memset(buffer_map, Y_VALUE, buffer_size);
		memset(buffer_map + u_offset, U_VALUE, u_size);
		memset(buffer_map + v_offset, V_VALUE, v_size);
	}

	// Create and add buffer 2
	
	data2.size = buffer_size;
	data2.flags = 0;
	data2.handle = 0;

	err = drmIoctl(drm_fd, DRM_IOCTL_SUN4I_GEM_CREATE, &data2);

	if (err) {
		printf("Failed to create 2nd Gem: %i\n", err);
		fflush(stdout);
	}

	const uint32_t bo_handles2[4] = { data2.handle, data2.handle, data2.handle, 0 };

	buf_id2 = 0;

	err = drmModeAddFB2(drm_fd, width, height, drm_mode_pixel_format, bo_handles2, pitches, offsets, &buf_id2, 0);

	if (err) {
		printf("Failed to add 2nd framebuffer %i\n", err);
		fflush(stdout);
	}

	// Map buffer 2
	
	buf_fd2 = 0;

	err = drmPrimeHandleToFD(drm_fd, data2.handle, DRM_RDWR, &buf_fd2);

	buffer_map2 = mmap(0, buffer_size, PROT_WRITE, MAP_SHARED, buf_fd2, 0);

	if (buffer_map2) {
		memset(buffer_map2, Y_VALUE, buffer_size);
		memset(buffer_map2 + u_offset, U_VALUE, u_size);
		memset(buffer_map2 + v_offset, V_VALUE, v_size);
	}

	// Create and add buffer 3

	data3.size = buffer_size;
	data3.flags = 0;
	data3.handle = 0;

	err = drmIoctl(drm_fd, DRM_IOCTL_SUN4I_GEM_CREATE, &data3);

	if (err) {
		printf("Failed to create 3st GEM. %i\n", err);
	}

        const uint32_t bo_handles3[4] = { data3.handle, data3.handle, data3.handle, 0 };

	buf_id3 = 0;

        err = drmModeAddFB2(drm_fd, width, height, drm_mode_pixel_format, bo_handles3, pitches, offsets, &buf_id3, 0);

	if (err) {
		printf("Failed to add 3st GEM. %i\n", err);
		fflush(stdout);
	}

	// Map buffer 3

	buf_fd3 = 0;

        err = drmPrimeHandleToFD(drm_fd, data3.handle, DRM_RDWR, &buf_fd3);

        buffer_map3 = mmap(0, buffer_size, PROT_WRITE, MAP_SHARED, buf_fd3, 0);

	if (buffer_map3) {
		memset(buffer_map3, Y_VALUE, buffer_size);
		memset(buffer_map3 + u_offset, U_VALUE, u_size);
		memset(buffer_map3 + v_offset, V_VALUE, v_size);
	}

	if (buf_id && new_plane) {
		run_video_update = 1;
		err = pthread_create(&display_thread, NULL, display_thread_loop, NULL);
	} else {
		printf("Skipped set plane due to empty buf_id\n");
		err = 1;
	}

	if (err) {
		printf("Failed to start draw thread\n");
	} else {
		printf("Display initialized\n");
		fflush(stdout);
	}
}

void terminate_display() 
{
	void *thread_return;

	run_video_update = 0;
	pthread_cond_signal(&display_buffer_cond);
	pthread_join(display_thread, &thread_return);

	if (new_plane && crtc) {
 		drmModeSetPlane(drm_fd, new_plane->plane_id, crtc->crtc_id, 0, 0, crtc->x, crtc->y, crtc->width, crtc->height, 0, 0, src_width, src_height);
	}

	pthread_mutex_destroy(&current_values_lock);
	pthread_cond_destroy(&available_buffer_cond);
}

void deallocate_buffers() {
	int err;
	struct drm_gem_close gem_close;

	memset(&gem_close, 0, sizeof(gem_close));

	if (buffer_map) {
		munmap(buffer_map, buffer_size);
	}

	if (buffer_map2) {
		munmap(buffer_map2, buffer_size);
	}

	if (buffer_map3) {
		munmap(buffer_map3, buffer_size);
	}

	close(buf_fd);
	close(buf_fd2);
	close(buf_fd3);

	if (buf_id) {
		drmModeRmFB(drm_fd, buf_id);
	}

	if (data.handle) {
		gem_close.handle = data.handle;
		err = drmIoctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
		if (err) {
			printf("Failed to close GEM #1\n");
		}
	}

	if (buf_id2) {
		drmModeRmFB(drm_fd, buf_id2);
	}

	if (data2.handle) {
		gem_close.handle = data2.handle;
		err = drmIoctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
		if (err) {
			printf("Failed to close GEM #2\n");
		}
	}

	if (buf_id3) {
		drmModeRmFB(drm_fd, buf_id3);
	}

	if (data3.handle) {
		gem_close.handle = data3.handle;
		err = drmIoctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
		if (err) {
			printf("Failed to close GEM #3\n");
		}
	}
}

void stop_drm() {
	if (new_plane) {
		drmModeSetPlane(drm_fd, new_plane->plane_id, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		drmModeFreePlane(new_plane);
	}

	if (old_plane) {
		drmModeSetPlane(drm_fd, old_plane->plane_id, crtc->crtc_id, old_plane->fb_id, 0, crtc->x, crtc->y, crtc->width, crtc->height, old_plane->x, old_plane->y, crtc->width, crtc->height);
		drmModeFreePlane(old_plane);
	}

	drmModeFreeCrtc(crtc);
	drmDropMaster(drm_fd);
	drmClose(drm_fd);
}

int get_dma_fd1() {
	return buf_fd;
}

int get_dma_fd2() {
	return buf_fd2;
}

int get_dma_fd3() {
	return buf_fd3;
}

void get_offsets(uint32_t *u_offset, uint32_t *v_offset) {
	*u_offset = data_offsets[0];
	*v_offset = data_offsets[1];
}
