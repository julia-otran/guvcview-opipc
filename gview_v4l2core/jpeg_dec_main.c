/*
 * Proof of Concept JPEG decoder using Allwinners CedarX
 *
 * WARNING: Don't use on "production systems". This was made by reverse
 * engineering and might crash your system or destroy data!
 * It was made only for my personal use of testing the reverse engineered
 * things, so don't expect good code quality. It's far from complete and
 * might crash if the JPEG doesn't fit it's requirements!
 *
 *
 *
 * Copyright (c) 2013 Jens Kuske <jenskuske@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <arm_neon.h>

#include "colorspaces.h"
#include "jpeg.h"
#include "ve.h"
#include "display.h"
#include "memory.h"
#include "granding.h"

void set_quantization_tables(struct jpeg_t *jpeg, void *regs)
{
	int i;
	for (i = 0; i < 64; i++)
		writel((uint32_t)(64 + i) << 8 | jpeg->quant[0]->coeff[i], regs + VE_MPEG_IQ_MIN_INPUT);
	for (i = 0; i < 64; i++)
		writel((uint32_t)(i) << 8 | jpeg->quant[1]->coeff[i], regs + VE_MPEG_IQ_MIN_INPUT);
}

void set_huffman_tables(struct jpeg_t *jpeg, void *regs)
{
	uint32_t buffer[512];
	memset(buffer, 0, 4*512);
	int i;
	for (i = 0; i < 4; i++)
	{
		if (jpeg->huffman[i])
		{
			int j, sum, last;

			last = 0;
			sum = 0;
			for (j = 0; j < 16; j++)
			{
				((uint8_t *)buffer)[i * 64 + 32 + j] = sum;
				sum += jpeg->huffman[i]->num[j];
				if (jpeg->huffman[i]->num[j] != 0)
					last = j;
			}
			memcpy(&(buffer[256 + 64 * i]), jpeg->huffman[i]->codes, sum);
			sum = 0;
			for (j = 0; j <= last; j++)
			{
				((uint16_t *)buffer)[i * 32 + j] = sum;
				sum += jpeg->huffman[i]->num[j];
				sum *= 2;
			}
			for (j = last + 1; j < 16; j++)
			{
				((uint16_t *)buffer)[i * 32 + j] = 0xffff;
			}
		}
	}

	for (i = 0; i < 512; i++)
	{
		writel(buffer[i], regs + VE_MPEG_RAM_WRITE_DATA);
	}
}

uint8_t get_format(struct jpeg_t *jpeg)
{
	uint8_t fmt = (jpeg->comp[0].samp_h << 4) | jpeg->comp[0].samp_v;

        switch (fmt)
        {
        case 0x11:
                return fmt;
                break;
        case 0x21:
                return fmt;
		break;
        case 0x12:
                return fmt;
		break;
        case 0x22:
                return fmt;
		break;
        }

	return 0;
}

void set_format(struct jpeg_t *jpeg, void *regs)
{
	uint8_t fmt = (jpeg->comp[0].samp_h << 4) | jpeg->comp[0].samp_v;

	switch (fmt)
	{
	case 0x11:
		writeb(0x1b, regs + VE_MPEG_TRIGGER + 0x3);
		break;
	case 0x21:
		writeb(0x13, regs + VE_MPEG_TRIGGER + 0x3);
		break;
	case 0x12:
		writeb(0x23, regs + VE_MPEG_TRIGGER + 0x3);
		break;
	case 0x22:
		writeb(0x03, regs + VE_MPEG_TRIGGER + 0x3);
		break;
	}
}

void set_size(struct jpeg_t *jpeg, void *regs)
{
	uint16_t h = (jpeg->height - 1) / (8 * jpeg->comp[0].samp_v);
	uint16_t w = (jpeg->width - 1) / (8 * jpeg->comp[0].samp_h);
	writel((uint32_t)h << 16 | w, regs + VE_MPEG_JPEG_SIZE);
}

static uint8_t display_initialized = 0;

static uint8_t *input_buffer = NULL;

static uint8_t *luma_output = NULL;
static uint8_t *chroma_u_output = NULL;
static uint8_t *chroma_v_output = NULL;

static uint8_t *luma_output1 = NULL;
static uint8_t *chroma_u_output1 = NULL;
static uint8_t *chroma_v_output1 = NULL;

static uint8_t *luma_output2 = NULL;
static uint8_t *chroma_u_output2 = NULL;
static uint8_t *chroma_v_output2 = NULL;

static uint8_t *luma_output3 = NULL;
static uint8_t *chroma_u_output3 = NULL;
static uint8_t *chroma_v_output3 = NULL;

static uint8_t *luma_output_virt = NULL;
static uint8_t *chroma_u_output_virt = NULL;
static uint8_t *chroma_v_output_virt = NULL;

static uint8_t *luma_output1_virt = NULL;
static uint8_t *chroma_u_output1_virt = NULL;
static uint8_t *chroma_v_output1_virt = NULL;

static uint8_t *luma_output2_virt = NULL;
static uint8_t *chroma_u_output2_virt = NULL;
static uint8_t *chroma_v_output2_virt = NULL;

static uint8_t *luma_output3_virt = NULL;
static uint8_t *chroma_u_output3_virt = NULL;
static uint8_t *chroma_v_output3_virt = NULL;

static uint8_t write_buffer = 0;

static void* ve_regs = NULL;

static uint8_t *phy_input;

void log_time(struct timespec *a, struct timespec *b) {
	long deltams = (b->tv_sec * 1000 + b->tv_nsec / 1000000) - (a->tv_sec * 1000 + a->tv_nsec / 1000000);
	// printf(": Step took %ld ms\n", deltams);
}

void hw_decode_jpeg(struct jpeg_t *jpeg)
{
	int width = jpeg->width;
	int height = jpeg->height;

	int v_offset = chroma_v_output - chroma_u_output;

	int result;

	if (luma_output == 0 || chroma_u_output == 0 || v_offset == 0) {
		printf("Bad output addresses. skipping decode.\n");
		return;
	}

	if (v_offset < 0) {
		printf("Bad chroma V output address, is less than choma u\n");
		return;
	}

	int input_size =(jpeg->data_len + 65535) & ~65535;
	int line_stride = ((jpeg->width + 31) & ~31);
	// uint8_t *input_buffer = ve_malloc(input_size);
	int output_size = line_stride * ((jpeg->height + 31) & ~31);
	// uint8_t *luma_output = ve_malloc(output_size);
	// uint8_t *chroma_output = ve_malloc(output_size);


	// printf("Will write to ctrl\n");
	// fflush(stdout);

	writel((0x3 << 4) | 0x3, ve_regs + VE_OUTPUT_FORMAT);
	writel(v_offset | (0x2 << 30), ve_regs + 0xe8);
	writel(output_size, ve_regs + 0xc4);
	writel(line_stride | (line_stride << 16), ve_regs + 0xc8);

	// activate MPEG engine
	// void *ve_regs = ve_get(VE_ENGINE_MPEG, 0);

	// set restart interval
	writel(jpeg->restart_interval, ve_regs + VE_MPEG_JPEG_RES_INT);

	// set JPEG format
	set_format(jpeg, ve_regs);

	// set output buffers (Luma / Croma)
	writel((uint32_t)luma_output, ve_regs + VE_MPEG_ROT_LUMA);
	writel((uint32_t)chroma_u_output, ve_regs + VE_MPEG_ROT_CHROMA);

	// set size
	set_size(jpeg, ve_regs);

	// ??
	writel(0x00000000, ve_regs + VE_MPEG_SDROT_CTRL);

	// input end
	writel((uint32_t)(phy_input + input_size - 1), ve_regs + VE_MPEG_VLD_END);

	// ??
	writel(0x0000007c, ve_regs + VE_MPEG_CTRL);

	// set input offset in bits
	writel(0 * 8, ve_regs + VE_MPEG_VLD_OFFSET);

	// set input length in bits
	writel(jpeg->data_len * 8, ve_regs + VE_MPEG_VLD_LEN);

	// set input buffer
	writel(((uint32_t)phy_input) | 0x70000000, ve_regs + VE_MPEG_VLD_ADDR);

	// set Quantisation Table
	set_quantization_tables(jpeg, ve_regs);

	// set Huffman Table
	writel(0x00000000, ve_regs + VE_MPEG_RAM_WRITE_PTR);
	set_huffman_tables(jpeg, ve_regs);

	// start
	writeb(0x0e, ve_regs + VE_MPEG_TRIGGER);

	// wait for interrupt
	result = ve_wait(1);

	// clean interrupt flag (??)
	writel(0x0000c00f, ve_regs + VE_MPEG_STATUS);
}

void hw_init(int width, int height) {
	printf("hw_init\n");
	fflush(stdout);
	ve_regs = ve_get(VE_ENGINE_MPEG, 0);

        int input_size = ((width * height * 3) + 65535) & ~65535;
        input_buffer = ve_malloc(input_size, 1);

	printf("Input buffer %p\n", input_buffer);
	fflush(stdout);
}

void hw_init_display(struct jpeg_t *jpeg) {
	init_display(jpeg->width, jpeg->height, get_format(jpeg));

	printf("Getting outputs\n");

	int dma_fd1 = get_dma_fd1();
	int dma_fd2 = get_dma_fd2();
	int dma_fd3 = get_dma_fd3();

	printf("Will get dma vaddr\n");
	fflush(stdout);

	void *vaddr1 = ve_get_dma_vaddr(dma_fd1);
	void *vaddr2 = ve_get_dma_vaddr(dma_fd2);
	void *vaddr3 = ve_get_dma_vaddr(dma_fd3);

	void *dma_phy1 = vaddr1 - 0xc0000000;
	void *dma_phy2 = vaddr2 - 0xc0000000;
	void *dma_phy3 = vaddr3 - 0xc0000000;

	uint32_t u_offset;
	uint32_t v_offset;

	get_offsets(&u_offset, &v_offset);

	luma_output1 = dma_phy1;
	chroma_u_output1 = luma_output1 + u_offset;
	chroma_v_output1 = luma_output1 + v_offset;

	luma_output2 = dma_phy2;
        chroma_u_output2 = luma_output2 + u_offset;
        chroma_v_output2 = luma_output2 + v_offset;

	luma_output3 = dma_phy3;
        chroma_u_output3 = luma_output3 + u_offset;
        chroma_v_output3 = luma_output3 + v_offset;

	luma_output = NULL;
	chroma_u_output = NULL;
	chroma_v_output = NULL;

	luma_output1_virt = get_buffer_1();
	chroma_u_output1_virt = luma_output1_virt + u_offset;
	chroma_v_output1_virt = luma_output1_virt + v_offset;

	luma_output2_virt = get_buffer_2();
	chroma_u_output2_virt = luma_output2_virt + u_offset;
	chroma_v_output2_virt = luma_output2_virt + v_offset;

	luma_output3_virt = get_buffer_3();
	chroma_u_output3_virt = luma_output3_virt + u_offset;
	chroma_v_output3_virt = luma_output3_virt + v_offset;

	luma_output_virt = NULL;
	chroma_u_output_virt = NULL;
	chroma_v_output_virt = NULL;

	display_initialized = 1;
	printf("Display initialize finished\n");
}

void hw_close() {
	ve_put();
	ve_free(input_buffer);
	terminate_display();
	ve_put_dma_vaddrs();
	deallocate_buffers();
	display_initialized = 0;
}

void get_buffer() {
	write_buffer = get_buffer_number();

	if (write_buffer == 2) {
                luma_output = luma_output2;
                chroma_u_output = chroma_u_output2;
                chroma_v_output = chroma_v_output2;

                luma_output_virt = luma_output2_virt;
                chroma_u_output_virt = chroma_u_output2_virt;
                chroma_v_output_virt = chroma_v_output2_virt;
	} else if (write_buffer == 3) {
                luma_output = luma_output3;
                chroma_u_output = chroma_u_output3;
                chroma_v_output = chroma_v_output3;

                luma_output_virt = luma_output3_virt;
                chroma_u_output_virt = chroma_u_output3_virt;
                chroma_v_output_virt = chroma_v_output3_virt;
	} else {
		luma_output = luma_output1;
		chroma_u_output = chroma_u_output1;
		chroma_v_output = chroma_v_output1;

                luma_output_virt = luma_output1_virt;
                chroma_u_output_virt = chroma_u_output1_virt;
                chroma_v_output_virt = chroma_v_output1_virt;
	}
}

void hw_decode_jpeg_main(uint8_t* data, long dataLen) {
        struct jpeg_t jpeg;
	uint8_t *virt_input;

        memset(&jpeg, 0, sizeof(jpeg));

        if (!parse_jpeg(&jpeg, data, dataLen)) {
                printf("ERROR: Can't parse JPEG\n");
		return;
	}

	if (!display_initialized) {
		if (get_format(&jpeg) == 0) {
			// This frame seems buggy, let's try another one
			printf("Invalid subsampling found!\n");
			return;
		}

		hw_init_display(&jpeg);
	}

	phy_input = (uint8_t*) ve_virt2phys(input_buffer);
	virt_input = input_buffer;
	// printf("Will do memcpy dst: %p src: %p len: %i\n", input_buffer, jpeg.data, jpeg.data_len);
	// fflush(stdout);
	memcpy(input_buffer, jpeg.data, jpeg.data_len);

	ve_flush_cache(virt_input, jpeg.data_len);

	get_buffer();
        hw_decode_jpeg(&jpeg);
	process_color_granding(jpeg.width, jpeg.height, luma_output_virt);
	put_buffer(write_buffer);
}

