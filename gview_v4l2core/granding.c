#include <arm_neon.h>

void process_color_granding(int width, int height, uint8_t *luma) {
	/*
	int total_pixels = width * height;
        int i;

        uint8_t sum_base_arr[16];

        for (i = 0; i < 16; i++) {
                sum_base_arr[i] = 20;
        }

        uint8x16_t sum_base = vld1q_u8(sum_base_arr);

        for (i = 0; i < total_pixels; i+=16) {
                uint8x16_t y_data = vld1q_u8(&luma[i]);
                y_data = vaddq_u8(y_data, sum_base);
                vst1q_u8(&luma[i], y_data);
        }
	*/
}

