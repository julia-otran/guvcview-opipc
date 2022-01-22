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

static struct drm_lima_gem_create data_y;
static struct drm_lima_gem_create data_u;
static struct drm_lima_gem_create data_v;

void init_display(int width, int height) {
	printf("Openning display\n");

	int err;

	drm_fd = drmOpen("lima", NULL);

        drmGetMagic(drm_fd, &drm_magic);
        err = drmAuthMagic(drm_fd, drm_magic);

	printf("AuthMagic returned: %p\n", err);

	printf("drm_fd %p\n", drm_fd);

	drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

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

	err = drmIoctl(drm_fd, DRM_IOCTL_LIMA_GEM_INFO, &info_y);

	drmIoctl(drm_fd, DRM_IOCTL_LIMA_GEM_INFO, &info_u);
	drmIoctl(drm_fd, DRM_IOCTL_LIMA_GEM_INFO, &info_v);

	printf("DRM_IOCTL_LIMA_GEM_INFO %i\n", err);

	const uint32_t bo_handles[4] = { info_y.handle, info_u.handle, info_v.handle, 0 };
	const uint32_t pitches[4] = { width, width / 2, width / 2, 0 };
	const uint32_t offsets[4] = { info_y.offset, info_u.offset, info_v.offset, 0 };

	printf("info_y handle %p \n", info_y.handle);

	printf("Adding FB2\n");

	drmModeAddFB2(drm_fd, width, height, DRM_FORMAT_YUV422, bo_handles, pitches, offsets, &buf_id, 0);

	int prime_y_fd, prime_u_fd, prime_v_fd;

	printf("info_y va %p\n", info_y.va);

	err = drmPrimeHandleToFD(drm_fd, info_y.handle, 0, &prime_y_fd);

	printf("drmPrimeHandleToFD return %i\n", err);

	drmPrimeHandleToFD(drm_fd, info_u.handle, 0, &prime_u_fd);
	drmPrimeHandleToFD(drm_fd, info_v.handle, 0, &prime_v_fd);

	printf("prime_y_fd %p \n", prime_y_fd);

	data_input[0] = mmap(NULL, data_y.size, PROT_WRITE, MAP_SHARED, prime_y_fd, info_y.offset);

	data_input[1] = mmap(NULL, data_u.size, PROT_WRITE, MAP_SHARED, prime_u_fd, info_u.offset);

	data_input[2] = mmap(NULL, data_v.size, PROT_WRITE, MAP_SHARED, prime_v_fd, info_v.offset);

	printf("mmap result: %p\n", data_input[0]);

	printf("Display initialized!!\n");
}

void terminate_display() 
{
	printf("Closing display\n");
	drmModeRmFB(drm_fd, buf_id);
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

