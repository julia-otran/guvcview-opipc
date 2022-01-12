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
static uint8_t *yPlane = NULL;
static uint8_t *uPlane = NULL;
static uint8_t *vPlane = NULL;
static tjhandle tj = NULL;

void output_ppm(struct jpeg_t *jpeg, uint8_t *luma_buffer, uint8_t *chroma_buffer, uint32_t *output)
{
	int x, x1, xMod32, y, cy, cy1, cy2, mcuW = 0;
	float Y, Cb, Cr = 0.0;

	uint32_t Y4, CbCrCbCr1, CbCrCbCr2;
	uint32_t cbcr_sum;
	uint32_t y_sum;

	uint32_t *y_out = yPlane;
	uint32_t *cb_out = uPlane;
	uint32_t *cr_out = vPlane;

	mcuW = ((jpeg->width + 31) & ~0x1f) * 32;

	for (y = 0; y < jpeg->height; y++)
	{
		for (x = 0; x < jpeg->width; x+=4)
		{
			// reordering and colorspace conversion should be done by Display Engine Frontend (DEFE)
			cy = y / jpeg->comp[0].samp_v;
			x1 = (x & ~ 0x1f) << 5;
			xMod32 = x & 0x1f;
			cy1 = ((cy & 0x1f) << 5);
			cy2 = (cy >>5) * mcuW;

			y_sum = x1 + xMod32 + ((y & 0x1f) << 5) + ((y >> 5) * mcuW);
			cbcr_sum = x1 + cy1 + cy2 + xMod32;
			cbcr_sum = cbcr_sum & ~0xF;

			// float Y = *((uint8_t *)(luma_buffer + x1 + xMod32 + ((y % 32) * 32) + ((y / 32) * mcuW)));
			Y4 = *((uint32_t *)(luma_buffer + y_sum));

			*y_out = Y4;
			y_out++;


			if ((x & 0x7) == 0) {
                        CbCrCbCr1 = *((uint32_t *)(chroma_buffer + (cbcr_sum)));
                        CbCrCbCr2 = *((uint32_t *)(chroma_buffer + (cbcr_sum) + 4));

			*cr_out = (CbCrCbCr1 & 0xFF000000) | ((CbCrCbCr1 & 0x0000ff00) << 8) | ((CbCrCbCr2 & 0xff000000) >> 16) | ((CbCrCbCr2 & 0x0000ff00) >> 8);
			*cb_out = ((CbCrCbCr1 & 0x00ff0000) << 8) | ((CbCrCbCr1 & 0x000000ff) << 16) | ((CbCrCbCr2 & 0x00ff0000) >> 8) | ((CbCrCbCr2 & 0x000000ff));

			cb_out++;
			cr_out++;
			}

			/*
			float Cb = *((uint8_t *)) - 128.0;
			float Cr = *((uint8_t *)crbSum + (xMod32 | 1)) - 128.0;

			float R = Y + 1.402 * Cr;
			float G = Y - 0.344136 * Cb - 0.714136 * Cr;
			float B = Y + 1.772 * Cb;

			if (R > 255.0) R = 255.0;
			else if (R < 0.0) R = 0.0;

			if (G > 255.0) G = 255.0;
			else if (G < 0.0) G = 0.0;

			if (B > 255.0) B = 255.0;
			else if (B < 0.0) B = 0.0;

			output[(y * 1920) + x] = ((uint8_t) R) | (((uint8_t) G) << 8) | (((uint8_t) B) << 16);
			*/
		}
	}

	unsigned char *srcPlanes[3] = { yPlane, uPlane, vPlane };
	tjDecodeYUVPlanes(tj, srcPlanes, NULL, TJSAMP_422, output, jpeg->width, jpeg->width * 4, jpeg->height, TJPF_RGBX, 0); 
}

void hw_decode_jpeg(struct jpeg_t *jpeg, uint8_t *output)
{
	int input_size =(jpeg->data_len + 65535) & ~65535;
	// uint8_t *input_buffer = ve_malloc(input_size);
	int output_size = ((jpeg->width + 31) & ~31) * ((jpeg->height + 31) & ~31);
	// uint8_t *luma_output = ve_malloc(output_size);
	// uint8_t *chroma_output = ve_malloc(output_size);
	memcpy(input_buffer, jpeg->data, jpeg->data_len);
	ve_flush_cache(input_buffer, jpeg->data_len);

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

        luma_output = ve_malloc(output_size);
        chroma_output = ve_malloc(output_size);

	yPlane = malloc(output_size);
	uPlane = malloc(output_size / 2);
	vPlane = malloc(output_size / 2);

	tj = tjInitDecompress();
}

void hw_close() {
	free(uPlane);
	free(vPlane);
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
       printf("hw_decode_jpeg before\n");
       hw_decode_jpeg(&jpeg, output);
       printf("hw_decode_jpeg after\n");
}

