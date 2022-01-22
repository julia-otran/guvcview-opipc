#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>
#include <drm/lima_drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "display.h"

#define PAGE_SIZE sysconf(_SC_PAGESIZE)

static drm_magic_t drm_magic;
static uint32_t data_input[3];

static uint32_t buf_id;
static int drm_fd;
static int dri_fd;

static struct drm_lima_ctx_create ctx;

static struct drm_lima_gem_create data_y;
static struct drm_lima_gem_create data_u;
static struct drm_lima_gem_create data_v;

static drmModePlane *current_plane;

static uint32_t old_plane_fb_id;

void init_display(int width, int height) {
	printf("Openning display\n");

	int err, i;

	drm_fd = drmOpen("lima", NULL);
	dri_fd = open("/dev/dri/card0", O_RDWR);

	drmVersion *ver = drmGetVersion(dri_fd);
	printf("driver name: %s\n", ver->name);

        drmGetMagic(drm_fd, &drm_magic);
        err = drmAuthMagic(drm_fd, drm_magic);

	printf("AuthMagic returned: %i\n", err);

	printf("drm_fd %x\n", drm_fd);

	drmIoctl(drm_fd, DRM_IOCTL_LIMA_CTX_CREATE, &ctx);

	drmSetClientCap(dri_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

        drmModePlaneRes *plane_res = NULL;
        drmModePlane *plane = NULL;

        plane_res = drmModeGetPlaneResources(dri_fd);

        if (plane_res) {
                for (i = 0; i < plane_res->count_planes; i++) {
                        plane = drmModeGetPlane(dri_fd, plane_res->planes[i]);
                        if (plane && plane->fb_id) {
                                break;
                        } else if (plane) {
                                drmModeFreePlane(plane);
                        }
                }

                drmModeFreePlaneResources(plane_res);
        } else {
                printf("cannot find plane_res\n");
        }

        current_plane = plane;

        if (plane) {
                old_plane_fb_id = plane->fb_id;
        } else {
                printf("No plane found!\n");
                old_plane_fb_id = 0;
        }

	data_y.size = ((width * height) + PAGE_SIZE) & ~PAGE_SIZE;
	data_y.flags = 0;
	data_y.handle = 0;
	data_y.pad = 0;

	err = drmIoctl(drm_fd, DRM_IOCTL_LIMA_GEM_CREATE, &data_y);

	printf("DRM_IOCTL_LIMA_GEM_CREATE output %i\n", err);

	data_u.size = ((width * height / 2) + PAGE_SIZE) & ~PAGE_SIZE;
        data_u.flags = 0;
        data_u.handle = 0;
	data_u.pad = 0;

	drmIoctl(drm_fd, DRM_IOCTL_LIMA_GEM_CREATE, &data_u);

        data_v.size = ((width * height / 2) + PAGE_SIZE) & ~PAGE_SIZE;
        data_v.flags = 0;
        data_v.handle = 0;
	data_v.pad = 0;

        drmIoctl(drm_fd, DRM_IOCTL_LIMA_GEM_CREATE, &data_v);

        struct drm_lima_gem_info info_y;
        struct drm_lima_gem_info info_u;
        struct drm_lima_gem_info info_v;

        info_y.handle = data_y.handle;
        info_u.handle = data_u.handle;
        info_v.handle = data_v.handle;

        drmIoctl(drm_fd, DRM_IOCTL_LIMA_GEM_INFO, &info_y);
        drmIoctl(drm_fd, DRM_IOCTL_LIMA_GEM_INFO, &info_u);
        drmIoctl(drm_fd, DRM_IOCTL_LIMA_GEM_INFO, &info_v);

        printf("info_y va %x\n", info_y.va);
        printf("info_y offset %llx\n", info_y.offset);

        const uint32_t bo_handles[4] = { data_y.handle, data_u.handle, data_v.handle, 0 };
        const uint32_t pitches[4] = { width, width / 2, width / 2, 0 };
        const uint32_t offsets[4] = { 0, 0, 0, 0 };

        printf("data_y handle %x \n", data_y.handle);

        printf("Adding FB2\n");

	buf_id = 0;

	drmSetMaster(dri_fd);

        err = drmModeAddFB2(dri_fd, width, height, DRM_FORMAT_YUV422, bo_handles, pitches, offsets, &buf_id, 0);

	printf("Add FB2 result %i\n", err);

	data_input[0] = mmap(NULL, data_y.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, info_y.offset);

	data_input[1] = mmap(NULL, data_u.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, info_u.offset);

	data_input[2] = mmap(NULL, data_v.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, info_v.offset);

	printf("Memory mapped\n");

	if (buf_id) {
		drmModeSetPlane(dri_fd, plane->plane_id, plane->crtc_id, buf_id, 0, 0, 0, width, height, 0, 0, width, height); 
	} else {
		printf("Skipped set plane due to empty buf_id\n");
	}

	printf("Display initialized!!\n");
}

void terminate_display() 
{
	printf("Closing display\n");

	if (current_plane) {
		if (old_plane_fb_id) {
		    drmModeSetPlane(dri_fd, current_plane->plane_id, current_plane->crtc_id, old_plane_fb_id, 0, 0, 0, 1920, 1080, 0, 0, 1920, 1080);
		}

		drmModeFreePlane(current_plane);
	}

	struct drm_lima_ctx_free ctx_free;
	ctx_free.id = ctx.id;
	ctx_free._pad = 0;

	drmModeRmFB(drm_fd, buf_id);
	drmIoctl(drm_fd, DRM_IOCTL_LIMA_CTX_FREE, &ctx_free);
	drmClose(drm_fd);
	close(dri_fd);

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

