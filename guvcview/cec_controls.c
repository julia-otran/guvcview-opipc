#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <linux/cec-funcs.h>
#include <pthread.h>
#include <math.h>

#include "atem.h"
#include "gviewv4l2core.h"

struct message_node {
	uint8_t code;
	uint8_t data1;
	uint8_t data2;
	uint8_t hasData2;
	struct message_node *next;
};

static int cec_fd = 0;

static uint8_t self_addr = 0;
static pthread_t rx_thread = 0;
static uint8_t rx_run = 0;

static struct message_node *message_queue_start = NULL;
static struct message_node *message_queue_end = NULL;

static pthread_mutex_t message_queue_mutex;

#define send_ioctl(r, p) int_ioctl(cec_fd, #r, r, p);
#define read_ioctl(r, p) int_ioctl(cec_fd, #r, r, p);

int int_ioctl(int fd, const char *name, unsigned long int request, void *param) {
	int ret = ioctl(fd, request, param);
	int err = errno;

	if (ret != 0) {
		printf("IOCTL Falied. Name: %s; Error %i\n", name, err);
		return err;
	}

	return 0;
}

void setup_cec() {
	struct cec_caps caps = { };
	struct cec_log_addrs laddrs = {};
	const char *osd_name = "BMD HDMI";

	send_ioctl(CEC_ADAP_G_CAPS, &caps);
	
	memset(&laddrs, 0, sizeof(laddrs));

	if (caps.capabilities & CEC_CAP_LOG_ADDRS) {
		// Clear state
		send_ioctl(CEC_ADAP_S_LOG_ADDRS, &laddrs);

		laddrs.cec_version = CEC_OP_CEC_VERSION_2_0;

		strncpy(laddrs.osd_name, osd_name, 9);
		laddrs.osd_name[8] = 0;

		laddrs.vendor_id = 0x7c2e0d;
		laddrs.flags = 0;
		laddrs.primary_device_type[0] = CEC_OP_PRIM_DEVTYPE_RECORD;
		laddrs.log_addr_type[0] = CEC_LOG_ADDR_TYPE_RECORD;
		laddrs.all_device_types[0] = CEC_OP_ALL_DEVTYPE_RECORD;
		
		laddrs.features[0][0] = CEC_OP_FEAT_RC_SRC_HAS_DEV_ROOT_MENU | CEC_OP_FEAT_RC_SRC_HAS_DEV_SETUP_MENU | CEC_OP_FEAT_RC_SRC_HAS_CONTENTS_MENU | CEC_OP_FEAT_RC_SRC_HAS_MEDIA_TOP_MENU | CEC_OP_FEAT_RC_SRC_HAS_MEDIA_CONTEXT_MENU;

		laddrs.features[0][1] = CEC_OP_FEAT_DEV_HAS_DECK_CONTROL | CEC_OP_FEAT_DEV_HAS_SET_AUDIO_RATE | CEC_OP_FEAT_DEV_SOURCE_HAS_ARC_RX;
		laddrs.num_log_addrs = 1;

		send_ioctl(CEC_ADAP_S_LOG_ADDRS, &laddrs);
	}
}

int detect_devices() {
	struct cec_msg msg;
	struct cec_log_addrs laddrs = { };

	send_ioctl(CEC_ADAP_G_LOG_ADDRS, &laddrs);

	self_addr = laddrs.log_addr[0];
	
	cec_msg_init(&msg, self_addr, CEC_LOG_ADDR_TV);
	send_ioctl(CEC_TRANSMIT, &msg);

	if (msg.tx_status & CEC_TX_STATUS_OK) {
		printf("Success found CEC TV device\n");
		return 0;
	}

	return -1;
}

void send_init_code() {
	struct cec_msg msg = {};
	uint8_t bytes[11];

	bytes[0] = 0x01;
	bytes[1] = 0x01;

	cec_msg_init(&msg, self_addr, CEC_LOG_ADDR_TV);
	cec_msg_vendor_command_with_id(&msg, 0x7c2e0d, 2, bytes);

	send_ioctl(CEC_TRANSMIT, &msg);
}

void add_message_to_queue(struct cec_msg *msg) {
	struct message_node *node;

	node = malloc(sizeof(node));
	node->code = msg->msg[1];
	node->data1 = msg->msg[2];
	node->data2 = msg->msg[3];
	node->hasData2 = msg->len >= 4;
	node->next = NULL;

	pthread_mutex_lock(&message_queue_mutex);

	if (message_queue_end == NULL) {
		message_queue_start = node;
	} else {
		message_queue_end->next = node;
	}
	
	message_queue_end = node;

	pthread_mutex_unlock(&message_queue_mutex);

	fflush(stdout);
}

void* rx_loop(void *args) {
	fd_set rd_fds;
	fd_set ex_fds;
	struct timeval tv;
	int res;

	tv.tv_sec = 0;
	tv.tv_usec = 100 * 1000;


	while (rx_run) {
		FD_ZERO(&rd_fds);
		FD_ZERO(&ex_fds);
		FD_SET(cec_fd, &rd_fds);
		FD_SET(cec_fd, &ex_fds);
		
		res = select(cec_fd + 1, &rd_fds, NULL, &ex_fds, &tv);

		if (res < 0) {
			printf("Select return error\n");
			break;
		}

		if (FD_ISSET(cec_fd, &rd_fds)) {
			struct cec_msg msg = { };

			res = read_ioctl(CEC_RECEIVE, &msg);

			if (res == ENODEV) {
				printf("CEC Device Disconnected!\n");
				break;
			}

			if (res != 0) {
				continue;
			}

			uint8_t from = cec_msg_initiator(&msg);

			if (from != CEC_LOG_ADDR_TV) {
				continue;
			}
			
			add_message_to_queue(&msg);
		}

		if (FD_ISSET(cec_fd, &ex_fds)) {
			struct cec_event ev;

			res = read_ioctl(CEC_DQEVENT, &ev);

			if (res != 0) {
				continue;
			}

			printf("Received CEC event\n");
		}
	}

	return NULL;
}

void set_mode_monitor() {
	unsigned int mode = CEC_MODE_MONITOR;
	send_ioctl(CEC_S_MODE, &mode);
}

void init_cec_controls() {
	cec_fd = open("/dev/cec0", O_RDWR);

	if (cec_fd <= 0) {
		printf("Failed to open CEC. skipping CEC ctrls.\n");
		return;
	}

	setup_cec();

	if (detect_devices() != 0) {
		printf("CEC destination device not found.\n");
		return;
	}

	send_init_code();

	set_mode_monitor();

	fcntl(cec_fd, F_SETFL, fcntl(cec_fd, F_GETFL) | O_NONBLOCK);

	message_queue_start = NULL;
	message_queue_end = NULL;
	rx_run = 1;

	pthread_mutex_init(&message_queue_mutex, NULL);
	pthread_create(&rx_thread, NULL, &rx_loop, NULL);

}

void stop_cec_controls() {
	void *val;

	rx_run = 0;

	if (rx_thread) {
		pthread_join(rx_thread, &val);
	}

	pthread_mutex_destroy(&message_queue_mutex);

	if (cec_fd) {
		close(cec_fd);
	}
}

int apply_value(v4l2_dev_t *my_vd, const char *name, int min, int max, int val) {
	 v4l2_ctrl_t *ctrl = v4l2core_get_control_list(my_vd);

	 double input_range = (double)(max - min);
	 double input_percent = (double)(val - min) / input_range;

	 double output_range;
	 double output;
	 int output_int;

 	 if ((min != 0 || max != 0) && (input_percent < 0.0 || input_percent > 1.0)) {
		printf("CEC input value for \"%s\" out of range: min %i max %i val %i\n", name, min, max, val);
	 }

	 if (min > max) {
		 input_percent = 1.0 - input_percent;
	 }

	 while (ctrl != NULL) {
		 if (
			(strcmp(ctrl->control.name, name) == 0) &&
			(
			 	(ctrl->control.type == V4L2_CTRL_TYPE_INTEGER) ||
				(ctrl->control.type == V4L2_CTRL_TYPE_U8) ||
				(ctrl->control.type == V4L2_CTRL_TYPE_U16) ||
				(ctrl->control.type == V4L2_CTRL_TYPE_U32)
			)
		 ) {
			 if (min == 0 && max == 0) {
				 // Just wrap and forward value
				 // I suspect that white balance temp will always match some value,
				 // given the color unit is the same (if that's not true, we will have to scale numbers
				if (val <= ctrl->control.minimum) {
					ctrl->value = ctrl->control.minimum;
				} else if (val >= ctrl->control.maximum) {
					ctrl->value = ctrl->control.maximum;
				} else {
					ctrl->value = (val / ctrl->control.step) * ctrl->control.step;
				}
			 } else if (val == min) {
				 ctrl->value = ctrl->control.minimum;
			 } else if (val == max) {
				 ctrl->value = ctrl->control.maximum;
			 } else {
				 output_range = (double) (ctrl->control.maximum - ctrl->control.minimum);
				 output = (output_range * input_percent) + ctrl->control.minimum;
				 output_int = (int) round(output);

				 if (output_int >= ctrl->control.minimum && output_int <= ctrl->control.maximum) {
					 ctrl->value = (output_int / ctrl->control.step) * ctrl->control.step;
				 } else {
					 printf("CEC ERROR: Calculated value %i is out of bounds of control \"%s\"\n", output_int, name);
				 }
			 }

			 v4l2core_set_control_value_by_id(my_vd, ctrl->control.id);
			 return 1;
		 }

		 ctrl = ctrl->next;
	 }

	 printf("CEC: No integer control for \"%s\" was found for connected camera. Skipping.\n", name);
	 return 0;
}

static uint8_t contrast_data2;

int process_contrast(v4l2_dev_t *my_vd, struct message_node *msg) {
	if (msg->hasData2 != 0) {
		contrast_data2 = msg->data2;
	}

	int val = (contrast_data2 << 8) | msg->data1;

	return apply_value(my_vd, "Contrast", 0, 4096, val);
}

static uint8_t saturation_data2;

int process_saturation(v4l2_dev_t *my_vd, struct message_node *msg) {
        if (msg->hasData2 != 0) {
                saturation_data2 = msg->data2;
        }

        int val = (saturation_data2 << 8) | msg->data1;

	return apply_value(my_vd, "Saturation", 0, 4096, val);
}

int process_wbt(v4l2_dev_t *my_vd, struct message_node *msg) {
	int color_temp = ((msg->data1 - 50) * 50) + 2500;
	return apply_value(my_vd, "White Balance Temperature", 0, 0, color_temp);
}

static uint8_t pivot_data2;

int process_pivot(v4l2_dev_t *my_vd, struct message_node *msg) {
        if (msg->hasData2 != 0) {
                pivot_data2 = msg->data2;
        }

        int val = (pivot_data2 << 8) | msg->data1;
	
	// I will map pivot to bright, it does not make sence, but I doubt that some webcam could have such control.
	return apply_value(my_vd, "Bright", 0, 2048, val);
}

static uint8_t luma_data2;

int process_luma(v4l2_dev_t *my_vd, struct message_node *msg) {
        if (msg->hasData2 != 0) {
                luma_data2 = msg->data2;
        }

        int val = (luma_data2 << 8) | msg->data1;

        // Don't know if there could be a luma ctrl, but i guess Gamma is more common.
        return apply_value(my_vd, "Gamma", 0, 2048, val);
}

static uint8_t shutter_data2;

int process_shutter_speed(v4l2_dev_t *my_vd, struct message_node *msg) {
        if (msg->hasData2 != 0) {
                shutter_data2 = msg->data2;
        }

        int val = (shutter_data2 << 8) | msg->data1;

        // This sounds like a commom feature to me, however my camera doesn't have such control.
	// I think the best way to map will be setting exposure.
	//
	// Note: Shutter speed: Greater, darker. Smaller, lighten.
	// Exposure is the inverse. greater is min, smaller is max
        return apply_value(my_vd, "Gain", 41667, 500, val);
}

int poll_cec_events(v4l2_dev_t *my_vd) {
	struct message_node *node;
	int changed = 0;

	do {
		pthread_mutex_lock(&message_queue_mutex);
		node = message_queue_start;
		
		if (node == NULL) {
			pthread_mutex_unlock(&message_queue_mutex);
			break;
		}

		message_queue_start = node->next;

		if (message_queue_start == NULL) {
			message_queue_end = NULL;
		}

		pthread_mutex_unlock(&message_queue_mutex);

		switch (node->code) {
			case CEC_DEV_CONTRAST_CODE:
				changed |= process_contrast(my_vd, node);
				break;
			case CEC_DEV_HUE_CODE:
				// Maybe someday I found a camera that has hue control.
				// As I don't know how this works in the camera side, don't know how implement.
				//
				// I also was wondering create a OpenGL shader to process the image,
				// maybe we could simulate all color granding features
				// (for a live event, I think it would be a nice feature).
				// But then there's the problem. 
				// How to get a physical address of a OGL PBO mapped buffer? (this is a discrete gpu, the phy address will always resolve to a cedar write capable address)
				// If anyone discover, please tell me. (I did some experiments with /proc/<pid>/pagemap, but I was unluckily. will try again, if it works may open the pandora box ahaha)
				//
				// Memcpy the codec output is tooo much slow. So need to decode direct to video buffer
				// 
				break;
			case CEC_DEV_LUMINOSITY_CODE:
				changed |= process_luma(my_vd, node);
				break;
			case CEC_DEV_PIVOT_CODE:
				changed |= process_pivot(my_vd, node);
				break;
			case CEC_DEV_SATURATION_CODE:
				changed |= process_saturation(my_vd, node);
				break;
			case CEC_DEV_SHUTTER_SPEED_CODE:
				changed |= process_shutter_speed(my_vd, node);
				break;
			case CEC_DEV_TINT_CODE:
				// TODO Too.
				break;
			case CEC_DEV_WHITE_BALANCE_TEMP:
				changed |= process_wbt(my_vd, node);
				break;
		}

		free(node);

		fflush(stdout);
	} while (node != NULL);

	return changed;
}

