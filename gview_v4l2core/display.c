#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <drm/sun4i_drm.h>

#include "display.h"

#define PAGE_SIZE sysconf(_SC_PAGESIZE)

#define VIRT_TO_PHYS (0xc0000000)

static uint32_t data_input[3];

static uint32_t buf_id;
static int drm_fd;
static int buf_fd;

static struct drm_sun4i_gem_create data;

static drmModePlane *old_plane;
static drmModePlane *new_plane;
static drmModeCrtc *crtc;

void init_display(int width, int height) {
	printf("Openning display\n");

	int err, i;

	drm_fd = drmOpen("sun4i-drm", NULL);

	drmVersion *ver = drmGetVersion(drm_fd);
	printf("driver name: %s\n", ver->name);

	printf("drm_fd %x\n", drm_fd);

	err = drmSetMaster(drm_fd);

	if (err) {
		printf("drm set master failed! %i\n", err);
	}

	drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	drmModeRes *resources = drmModeGetResources(drm_fd);

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
                        }
                }
        } else {
                printf("cannot find plane_res\n");
        }

        if (plane) {
                old_plane = plane;
		printf("Current CRTC: %i\n", plane->crtc_id);
        } else {
                printf("No plane found!\n");
                old_plane = NULL;
        }

	int j;

	crtc = drmModeGetCrtc(drm_fd, plane->crtc_id);

	new_plane = NULL;

	printf("count planes %i\n", plane_res->count_planes);

	for (i = 0; i < plane_res->count_planes; i++) {
		plane = drmModeGetPlane(drm_fd, plane_res->planes[i]);

		if (plane) {
			printf("Count formats %i\n", plane->count_formats);

			for (j = 0; j < plane->count_formats; j++) {
				if (plane->formats[j] == DRM_FORMAT_YUV422) {
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
		printf("Failed to find plane with format YUV422\n");
		return;
	}

	uint32_t size_page_aligned = ((width * height) + PAGE_SIZE) & ~PAGE_SIZE;
	uint32_t u_offset = size_page_aligned;
	uint32_t v_offset = u_offset + (width * height / 2);
	uint32_t total_size = v_offset + (width * height / 2);

	data.size = (total_size + PAGE_SIZE) & ~PAGE_SIZE;
	data.flags = 0;
	data.handle = 0;

	err = drmIoctl(drm_fd, DRM_IOCTL_SUN4I_GEM_CREATE, &data);

	printf("DRM_IOCTL_SUN4I_GEM_CREATE output %i\n", err);

        const uint32_t bo_handles[4] = { data.handle, data.handle, data.handle, 0 };
        const uint32_t pitches[4] = { width, width / 2, width / 2, 0 };
        const uint32_t offsets[4] = { 0, u_offset, v_offset, 0 };

        printf("Adding FB2\n");

	buf_id = 0;

        err = drmModeAddFB2(drm_fd, width, height, DRM_FORMAT_YUV422, bo_handles, pitches, offsets, &buf_id, 0);

	printf("Add FB2 result %i\n", err);

	buf_fd = 0;

	err = drmPrimeHandleToFD(drm_fd, data.handle, DRM_RDWR, &buf_fd);

	printf("Prime handle to FD result %i\n", err);

	void *addr = mmap(0, data.size, PROT_WRITE, MAP_SHARED, buf_fd, 0);

	printf("mmap addr: %x\n", addr);

	memset(addr, 100, data.size);

	data_input[0] = addr;
	data_input[1] = addr + u_offset;
	data_input[2] = addr + v_offset;

	printf("Memory mapped\n");

	printf("Will set plane\n");

	if (buf_id && new_plane) {
		err = drmModeSetPlane(drm_fd, old_plane->plane_id, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		printf("Clear plane crtc %i\n", err);

		err = drmModeSetPlane(drm_fd, new_plane->plane_id, crtc->crtc_id, buf_id, 0, crtc->x, crtc->y, crtc->width, crtc->height, 0, 0, width << 16, height << 16); 
		printf("drm set plane %i\n", err);
	} else {
		printf("Skipped set plane due to empty buf_id\n");
	}

	printf("Display initialized!!\n");
}

void terminate_display() 
{
	printf("Closing display\n");

	if (new_plane) {
		drmModeSetPlane(drm_fd, new_plane->plane_id, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		drmModeFreePlane(new_plane);
	}

	if (old_plane) {

		drmModeSetPlane(drm_fd, old_plane->plane_id, crtc->crtc_id, old_plane->fb_id, 0, crtc->x, crtc->y, crtc->width, crtc->height, old_plane->x, old_plane->y, crtc->width, crtc->height);
		drmModeFreePlane(old_plane);
	}

	drmModeFreeCrtc(crtc);

	if (buf_id) {
		drmModeRmFB(drm_fd, buf_id);
	}

	drmDropMaster(drm_fd);
	drmClose(drm_fd);

}

void get_output(uint32_t *out_ptr) {
	if (data_input[0] != -1 && data_input[1] != -1 && data_input[2] != -1) {
		out_ptr[0] = data_input[0];
		out_ptr[1] = data_input[1];
		out_ptr[2] = data_input[2];
	} else {
		out_ptr[0] = NULL;
		out_ptr[1] = NULL;
		out_ptr[2] = NULL;
	}
}

