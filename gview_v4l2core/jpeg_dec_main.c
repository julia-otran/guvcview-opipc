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
#include <turbojpeg.h>

#include "colorspaces.h"
#include "jpeg.h"
#include "ve.h"

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

static uint8_t *input_buffer = NULL;
static uint8_t *luma_output = NULL;
static uint8_t *chroma_output = NULL;
static void* ve_regs = NULL;
static tjhandle tj = NULL;

void output_ppm(struct jpeg_t *jpeg, uint8_t *luma_buffer, uint8_t *chroma_buffer, uint8_t *output)
{
	
	int line_stride = ((jpeg->width + 31) & ~31);
        int output_size = line_stride * ((jpeg->height + 31) & ~31);
	uint32_t *output_p = (uint32_t*)output;

	// unsigned char *srcPlanes[3] = { luma_buffer, chroma_buffer, (chroma_buffer + output_size) };
	// tjDecodeYUVPlanes(tj, srcPlanes, NULL, TJSAMP_422, output, jpeg->width, jpeg->width * 4, jpeg->height, TJPF_RGBX, 0); 
	//
	// memcpy(output, luma_buffer, 1920*1080);
	// memcpy((output+1920*1080), chroma_buffer, 1920*1080);
	output_p[0] = (uint32_t)luma_buffer;
	output_p[1] = (uint32_t)chroma_buffer;
	output_p[2] = ((uint32_t)chroma_buffer) + (output_size / 2);
}

void hw_decode_jpeg(struct jpeg_t *jpeg, uint8_t *output)
{
	int input_size =(jpeg->data_len + 65535) & ~65535;
	int line_stride = ((jpeg->width + 31) & ~31);
	// uint8_t *input_buffer = ve_malloc(input_size);
	int output_size = line_stride * ((jpeg->height + 31) & ~31);
	// uint8_t *luma_output = ve_malloc(output_size);
	// uint8_t *chroma_output = ve_malloc(output_size);
	memcpy(input_buffer, jpeg->data, jpeg->data_len);
	ve_flush_cache(input_buffer, jpeg->data_len);

	writel((0x3 << 4) | 0x3, ve_regs + VE_OUTPUT_FORMAT);
	writel((output_size / 2) | (0x2 << 30), ve_regs + 0xe8);
	writel(output_size, ve_regs + 0xc4);
	writel(line_stride | (line_stride << 16), ve_regs + 0xc8);

	// activate MPEG engine
	// void *ve_regs = ve_get(VE_ENGINE_MPEG, 0);

	// set restart interval
	writel(jpeg->restart_interval, ve_regs + VE_MPEG_JPEG_RES_INT);

	// set JPEG format
	set_format(jpeg, ve_regs);

	// set output buffers (Luma / Croma)
	writel(ve_virt2phys(luma_output), ve_regs + VE_MPEG_ROT_LUMA);
	writel(ve_virt2phys(chroma_output), ve_regs + VE_MPEG_ROT_CHROMA);

	// set size
	set_size(jpeg, ve_regs);

	// ??
	writel(0x00000000, ve_regs + VE_MPEG_SDROT_CTRL);

	// input end
	writel(ve_virt2phys(input_buffer) + input_size - 1, ve_regs + VE_MPEG_VLD_END);

	// ??
	writel(0x0000007c, ve_regs + VE_MPEG_CTRL);

	// set input offset in bits
	writel(0 * 8, ve_regs + VE_MPEG_VLD_OFFSET);

	// set input length in bits
	writel(jpeg->data_len * 8, ve_regs + VE_MPEG_VLD_LEN);

	// set input buffer
	writel(ve_virt2phys(input_buffer) | 0x70000000, ve_regs + VE_MPEG_VLD_ADDR);

	// set Quantisation Table
	set_quantization_tables(jpeg, ve_regs);

	// set Huffman Table
	writel(0x00000000, ve_regs + VE_MPEG_RAM_WRITE_PTR);
	set_huffman_tables(jpeg, ve_regs);

	// start
	writeb(0x0e, ve_regs + VE_MPEG_TRIGGER);

	// wait for interrupt
	ve_wait(1);

	// clean interrupt flag (??)
	writel(0x0000c00f, ve_regs + VE_MPEG_STATUS);

	ve_flush_cache(luma_output, output_size);
	ve_flush_cache(chroma_output, output_size * 2);
	output_ppm(jpeg, luma_output, chroma_output, output);

	// ve_free(input_buffer);
	// ve_free(luma_output);
	// ve_free(chroma_output);
}

void hw_init(int width, int height) {
        if (!ve_open())
                err(EXIT_FAILURE, "Can't open VE");

	ve_regs = ve_get(VE_ENGINE_MPEG, 0);

        int input_size = ((width * height * 3) + 65535) & ~65535;
        input_buffer = ve_malloc(input_size);
        
	int output_size = ((width + 31) & ~31) * ((height + 31) & ~31);

        luma_output = ve_malloc(output_size * 3);
        // chroma_output = ve_malloc(output_size * 3);
	chroma_output = luma_output + output_size;

	// tj = tjInitDecompress();
}

void hw_close() {
	tjFree(tj);
	ve_put();
	ve_free(input_buffer);
	ve_free(luma_output);
	ve_free(chroma_output);
	ve_close();
}

void hw_decode_jpeg_main(uint8_t* data, long dataLen, uint8_t* output) {
        struct jpeg_t jpeg ;
        memset(&jpeg, 0, sizeof(jpeg));
        if (!parse_jpeg(&jpeg, data, dataLen))
                printf("ERROR: Can't parse JPEG\n");

       // dump_jpeg(&jpeg);
       // printf("hw_decode_jpeg before\n");
       hw_decode_jpeg(&jpeg, output);
       // printf("hw_decode_jpeg after\n");
}

