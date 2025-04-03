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
#include <stdlib.h>
#include <string.h>
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

static drmModePlane **new_planes;
static int count_crtcs;
static drmModeCrtc **crtcs;

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
			forward(current_display_buffer);

		} else {
			should_draw = 0;
		}

		pthread_mutex_unlock(&current_values_lock);

		if (should_draw) {
			if (crtcs[0] && new_planes[0]) {
				result = drmModeSetPlane(drm_fd, new_planes[0]->plane_id, crtcs[0]->crtc_id, buf_ids[display_buffer], 0, crtcs[0]->x, crtcs[0]->y, crtcs[0]->width, crtcs[0]->height, 0, 0, src_width, src_height);

				if (result) {
					printf("Setting HDMI plane failed %i\n", result);
					fflush(stdout);
					exit(1);
				}
			}

			if (crtcs[1] && new_planes[1]) {
				result = drmModeSetPlane(drm_fd, new_planes[1]->plane_id, crtcs[1]->crtc_id, buf_ids[display_buffer], 0, crtcs[1]->x, crtcs[1]->y, crtcs[1]->width, crtcs[1]->height, 0, 0, src_width, src_height);

				if (result) {
					printf("Setting composite plane failed %i\n", result);
					fflush(stdout);
					exit(1);
				}
			}
		}

		if (prev_display_buffer) {
			pthread_mutex_lock(&current_values_lock);

			put(current_available_buffer, prev_display_buffer);

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
		forward(current_available_buffer);
	} else if (current_display_buffer[0]) {
		printf("Waiting next frame. Is draw thread too slow?\n");
		pthread_cond_wait(&available_buffer_cond, &current_values_lock);

		write_buffer = current_available_buffer[0];
		forward(current_available_buffer);
	} else {
		printf("Failed to dequeue available buffer\n");
		write_buffer = 0;
	}

	pthread_mutex_unlock(&current_values_lock);

	return write_buffer;

}

void put_buffer(uint8_t buffer_number) {
	pthread_mutex_lock(&current_values_lock);

	put(current_display_buffer, buffer_number);

	pthread_cond_signal(&display_buffer_cond);

	pthread_mutex_unlock(&current_values_lock);
}

void start_drm() {
	int err = 0;
	int i = 0;
	int j = 0;

	int composite_connector_id = 0;
	int hdmi_connector_id = 0;

	int composite_encoder_id = 0;
	int hdmi_encoder_id = 0;

	int composite_crtc_id = 0;
	int hdmi_crtc_id = 0;

	int hdmi_buffer_id = 0;
	int composite_buffer_id = 0;

	drmModeModeInfo composite_mode;
	drmModeModeInfo hdmi_mode;

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

	drmModeRes *resources = drmModeGetResources(drm_fd);

	printf("Connectors....\n");
	drmModeConnector *conn;

	for (i=0; i < resources->count_connectors; i++) {
		conn = drmModeGetConnector(drm_fd, resources->connectors[i]);
		printf("------------\n");
		printf("Connector: %i\n", conn->connector_id);
		printf("Type: %i\n", conn->connector_type);
		printf("Current Encoder: %i\n\n", conn->encoder_id);

		for (int j=0; j < conn->count_encoders; j++) {
			printf("Encoder: %i\n", conn->encoders[j]);
		}

		printf("\n");

		for (int j=0; j < conn->count_modes; j++) {
			drmModeModeInfo *info = &(conn->modes[j]);

			printf("Mode: %i;%i; %i;%i name: %s\n", info->hdisplay, info->vdisplay, info->hsync_end, info->vsync_end, info->name);
		}

		if (conn->connector_type == DRM_MODE_CONNECTOR_Composite) {
			composite_connector_id = conn->connector_id;
			composite_encoder_id = conn->encoder_id;
			memcpy(&composite_mode, &conn->modes[0], sizeof(composite_mode));
		}

		if (conn->connector_type == DRM_MODE_CONNECTOR_HDMIA) {
			hdmi_connector_id = conn->connector_id;
			hdmi_encoder_id = conn->encoder_id;
			memcpy(&hdmi_mode, &conn->modes[0], sizeof(hdmi_mode));
		}

		drmModeFreeConnector(conn);
	}
	printf("------------\n");
	printf("Done found connectors\n");

	printf("Encoders....\n");
	drmModeEncoder *enc;

	for (i=0; i < resources->count_encoders; i++) {
		enc = drmModeGetEncoder(drm_fd, resources->encoders[i]);
		printf("------------\n");
		printf("Encoder: %i\n", enc->encoder_id);
		printf("CRTC: %i\n", enc->crtc_id);
		printf("Possible CRTCs: %i\n", enc->possible_crtcs);
		printf("Possible clones: %i\n", enc->possible_clones);

		if (enc->encoder_id == composite_encoder_id) {
			composite_crtc_id = enc->crtc_id;
		};

		if (enc->encoder_id == hdmi_encoder_id) {
			hdmi_crtc_id = enc->crtc_id;
		};

		drmModeFreeEncoder(enc);
	}
	printf("------------\n");
	printf("Done found encoders\n");

	count_crtcs = resources->count_crtcs;
	crtcs = (drmModeCrtc**) calloc(count_crtcs, sizeof(drmModeCrtc*));

	drmModeCrtc *crtc;

	printf("CRTCs.....\n");
	for (i=0; i < resources->count_crtcs; i++) {
		crtc = drmModeGetCrtc(drm_fd, resources->crtcs[i]);

		if (crtc->crtc_id == hdmi_crtc_id) {
			hdmi_buffer_id = crtc->buffer_id;
		}

		if (crtc->crtc_id == composite_crtc_id) {
			composite_buffer_id = crtc->buffer_id;
			drmModeSetCrtc(drm_fd, composite_crtc_id, composite_buffer_id, 0, 0, &composite_connector_id, 1, &composite_mode);
			drmModeFreeCrtc(crtc);
			crtc = drmModeGetCrtc(drm_fd, resources->crtcs[i]);
		}

		printf("------------\n");
		printf("CRTC Id: %i\n", crtc->crtc_id);
		printf("CRTC X: %i\n", crtc->x);
		printf("CRTC Y: %i\n", crtc->y);
		printf("CRTC W: %i\n", crtc->width);
		printf("CRTC H: %i\n", crtc->height);

		crtcs[i] = crtc;
	}

	printf("------------\n");
	printf("Done found CRTCs\n");

	if (count_crtcs <= 0) {
		printf("Failed to find CRTC\n");
		fflush(stdout);
	}

	drmModeFreeResources(resources);

	// List and clear current planes;
	printf("Findig planes....\n");

        drmModePlaneRes *plane_res = NULL;
        drmModePlane *plane = NULL;

        plane_res = drmModeGetPlaneResources(drm_fd);

	for (i = 0; i < plane_res->count_planes; i++) {
		plane = drmModeGetPlane(drm_fd, plane_res->planes[i]);

		if (plane) {
			printf("-----------\n");
			printf("Plane ID: %i\n", plane->plane_id);
			printf("CRCT ID: %i\n", plane->crtc_id);
			printf("Possible CRTCs: %i\n", plane->possible_crtcs);

			if (plane->fb_id) {
				printf("Clearing plane: %i....\n", plane->plane_id);
				fflush(stdout);

				drmModeSetPlane(drm_fd, plane->plane_id, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
			}

			drmModeFreePlane(plane);
		}
	}

	printf("DRM Started!\n");
	fflush(stdout);
}

void find_new_plane() {
	int i, j, crtc_idx, has_format;

        drmModePlaneRes *plane_res = NULL;
	drmModePlane *plane = NULL;

        plane_res = drmModeGetPlaneResources(drm_fd);

	new_planes = (drmModePlane**) calloc(count_crtcs, sizeof(drmModePlane*));
	memset(new_planes, 0, count_crtcs * sizeof(drmModePlane*));

	for (i = 0; i < plane_res->count_planes; i++) {
		has_format = 0;
		crtc_idx = -1;

		plane = drmModeGetPlane(drm_fd, plane_res->planes[i]);

		if (plane) {
			for (j = 0; j < plane->count_formats; j++) {
				if (plane->formats[j] == drm_mode_pixel_format) {
					has_format = 1;
					break;
				}
			}

			printf("-----------\n");
			printf("Plane ID: %i\n", plane->plane_id);
			printf("Support format: %i\n", has_format);
			printf("Possible CRTCs: %i\n", plane->possible_crtcs);

			if (has_format) {
				for (j = 0; j < count_crtcs; j++) {
					if (plane->possible_crtcs & (1 << j)) {
						new_planes[j] = plane;
					}
				}

				continue;
			}

			drmModeFreePlane(plane);
		}
	}

	drmModeFreePlaneResources(plane_res);

	int any_found = 0;

	for (i = 0; i < count_crtcs; i++) {
		if (new_planes[i]) {
			any_found = 1;
			printf("Found plane %i for CRTC %i\n", new_planes[i]->plane_id, crtcs[i]->crtc_id);
		} else {
			printf("Could not find plane for CRTC: %i\n", crtcs[i]->crtc_id);
		}
	}

	if (!any_found) {
		printf("Unable to find a available plane\n");
		fflush(stdout);
		exit(1);
	}
}

void setPlanesColorFormat() {
	int err = 0;
	int i, j, k;
	__u32 prop_ids[2][2] = { { 0, 0 }, { 0, 0 } };
	__u64 prop_values[2][2] = { { 0, 0 }, { 0, 0 } };

	printf("\n\nLooking for color mode and range props\n");
	fflush(stdout);

	// Get Color Mode and Color Range prop ids

	struct drm_mode_obj_get_properties obj_get_props_data;

	__u32 *props_ptr = (__u32*) calloc(500, sizeof(__u32));
	__u64 *prop_values_ptr = (__u64*) calloc(500, sizeof(__u64));

	struct drm_mode_get_property property;
	struct drm_mode_property_enum *prop_enum;

	struct drm_mode_property_enum *enum_blob_ptr = (struct drm_mode_property_enum*)
		calloc(10, sizeof(struct drm_mode_property_enum));

	// Should we support more than 2 crtcs?
	for (i = 0; i < 2; i++) {
		if (new_planes[i]) {
			printf("Getting properties of plane %i\n", new_planes[i]->plane_id);

			obj_get_props_data.obj_id = new_planes[i]->plane_id;
			obj_get_props_data.obj_type = DRM_MODE_OBJECT_PLANE;
			obj_get_props_data.count_props = 500;
			obj_get_props_data.props_ptr = (__u64) ((__u32)props_ptr);
			obj_get_props_data.prop_values_ptr = (__u64) ((__u32)prop_values_ptr);

			err = drmIoctl(drm_fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &obj_get_props_data);

			if (err) {
				printf("Error getting properties of plane %i. Err: %i\n", new_planes[i]->plane_id, err);
				continue;
			}

			for (j = 0; j < obj_get_props_data.count_props; j++) {
				if (props_ptr[j]) {
					property.enum_blob_ptr = (__u64) ((__u32)enum_blob_ptr);
					property.count_enum_blobs = 10;
					property.count_values = 0;
					property.flags = 0;
					property.prop_id = props_ptr[j];

					memset(&property.name, 0, sizeof(property.name));

					err = drmIoctl(drm_fd, DRM_IOCTL_MODE_GETPROPERTY, &property);

					if (err) {
						printf("Error getting property %u. Err: %i\n", props_ptr[j], err);
						continue;
					}

					printf("Found property %u: %s\n", props_ptr[j], property.name);

					if (strcmp(property.name, "COLOR_ENCODING") == 0) {
						printf("Found plane %i COLOR_ENCODING prop. id: %u\n", new_planes[i]->plane_id, property.prop_id);

						for (k = 0; k < 5; k++) {
							prop_enum = &enum_blob_ptr[k];

							if (prop_enum && strstr(prop_enum->name, "601")) {
								printf("Prop enum name %s value %llu\n", prop_enum->name, prop_enum->value);
								prop_ids[i][0] = property.prop_id;
								prop_values[i][0] = prop_enum->value;
								break;
							}
						}
					}

					if (strcmp(property.name, "COLOR_RANGE") == 0) {
						printf("Found plane %i COLOR_RANGE prop. id: %u\n", new_planes[i]->plane_id, property.prop_id);

						for (k = 0; k < 5; k++) {
							prop_enum = &enum_blob_ptr[k];

							if (prop_enum && strstr(prop_enum->name, "full")) {
								printf("Prop enum name %s value %llu\n", prop_enum->name, prop_enum->value);
								prop_ids[i][1] = property.prop_id;
								prop_values[i][1] = prop_enum->value;
								break;
							}
						}
					}

					if (prop_ids[i][0] && prop_ids[i][1]) {
						break;
					}
				}
			}
		}
	}

	fflush(stdout);

	free(props_ptr);
	free(prop_values_ptr);
	free(enum_blob_ptr);

	struct drm_mode_obj_set_property set_prop;

	for (i = 0; i < 2; i++) {
		for (j = 0; j < 2; j++) {
			if (prop_ids[i][j]) {
				set_prop.obj_id = new_planes[i]->plane_id;
				set_prop.obj_type = DRM_MODE_OBJECT_PLANE;
				set_prop.prop_id = prop_ids[i][j];
				set_prop.value = prop_values[i][j];

				err = drmIoctl(drm_fd, DRM_IOCTL_MODE_OBJ_SETPROPERTY, &set_prop);

				if (err) {
					printf("Failed setting prop %u. Err: %i\n", prop_ids[i][j], err);
				} else {
					printf("Prop %u set to value %llu\n", prop_ids[i][j], prop_values[i][j]);
				}
			}
		}
	}

	printf("Done color mode settings\n\n");
	fflush(stdout);
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

	printf("Calculated buffer size and offsets\n");
	fflush(stdout);

	// Create and add buffer 1

	data.size = buffer_size;
	data.flags = 0;
	data.handle = 0;

	err = drmIoctl(drm_fd, DRM_IOCTL_SUN4I_GEM_CREATE, &data);

	if (err) {
		printf("Failed to create 1st GEM. %i\n", err);
		fflush(stdout);
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
		fflush(stdout);
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

	if (buf_id) {
		printf("Setting color format on planes\n");
		fflush(stdout);
		setPlanesColorFormat();

		printf("Starting display thread\n");
		fflush(stdout);
		run_video_update = 1;
		err = pthread_create(&display_thread, NULL, display_thread_loop, NULL);
	} else {
		printf("Skipped starting display thread\n");
		err = 1;
	}

	if (err) {
		printf("Failed to start draw thread\n");
		fflush(stdout);
		exit(1);
	} else {
		printf("Display initialized\n");
		fflush(stdout);
	}
}

void terminate_display()
{
	void *thread_return;

	if (src_width == 0 && src_height == 0) {
		return;
	}

	run_video_update = 0;
	pthread_cond_signal(&display_buffer_cond);
	pthread_join(display_thread, &thread_return);

	src_width = 0;
	src_height = 0;

	for (int i = 0; i < count_crtcs; i++) {
		if (new_planes[i]) {
			drmModeSetPlane(drm_fd, new_planes[i]->plane_id, crtcs[i]->crtc_id, 0, 0, crtcs[i]->x, crtcs[i]->y, crtcs[i]->width, crtcs[i]->height, 0, 0, src_width, src_height);
		}
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
	if (new_planes) {
		for (int i = 0; i < count_crtcs; i++) {
			if (new_planes[i]) {
				drmModeSetPlane(drm_fd, new_planes[i]->plane_id, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
				drmModeFreePlane(new_planes[i]);
			}

			drmModeSetPlane(drm_fd, 0, crtcs[i]->crtc_id, 0, 0, crtcs[i]->x, crtcs[i]->y, crtcs[i]->width, crtcs[i]->height, 0, 0, crtcs[i]->width, crtcs[i]->height);
			drmModeFreeCrtc(crtcs[i]);
		}
	}

	drmDropMaster(drm_fd);
	drmClose(drm_fd);
}

int get_dma_fd1() {
	return buf_fd;
}

uint8_t* get_buffer_1() {
	return (uint8_t*)buffer_map;
}

uint8_t* get_buffer_2() {
	return (uint8_t*)buffer_map2;
}

uint8_t* get_buffer_3() {
	return (uint8_t*)buffer_map3;
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
