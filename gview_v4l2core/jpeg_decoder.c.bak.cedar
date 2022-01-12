/*******************************************************************************#
#           guvcview              http://guvcview.sourceforge.net               #
#                                                                               #
#           Paulo Assis <pj.assis@gmail.com>                                    #
#           Nobuhiro Iwamatsu <iwamatsu@nigauri.org>                            #
#                             Add UYVY color support(Macbook iSight)            #
#                                                                               #
# This program is free software; you can redistribute it and/or modify          #
# it under the terms of the GNU General Public License as published by          #
# the Free Software Foundation; either version 2 of the License, or             #
# (at your option) any later version.                                           #
#                                                                               #
# This program is distributed in the hope that it will be useful,               #
# but WITHOUT ANY WARRANTY; without even the implied warranty of                #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                 #
# GNU General Public License for more details.                                  #
#                                                                               #
# You should have received a copy of the GNU General Public License             #
# along with this program; if not, write to the Free Software                   #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA     #
#                                                                               #
********************************************************************************/

/*******************************************************************************#
#                                                                               #
#  M/Jpeg decoding and frame capture taken from luvcview                        #
#                                                                               #
********************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "gviewv4l2core.h"
#include "colorspaces.h"
#include "jpeg_decoder.h"
#include "gview.h"
#include "../config.h"

#include "/home/orangepi/media-codec/sunxi-cedarx/SOURCE/vdecoder/include/vdecoder.h"

extern int verbosity;

typedef struct _jpeg_decoder_context_t
{
	int width;
	int height;
	int pic_size;
	
} jpeg_decoder_context_t;

static jpeg_decoder_context_t *jpeg_ctx = NULL;

static VideoDecoder *video_decoder = NULL;
static VideoStreamInfo *video_stream_info = NULL;
static VConfig *video_config = NULL;
static VideoStreamDataInfo *video_stream_data_info = NULL;

static char *video_decoder_buffer = NULL;
static int video_decoder_buffer_size = 0;
static char *video_decoder_ring_buffer = NULL;
static int video_decoder_ring_buffer_size = 0;

/*
 * init (m)jpeg decoder context
 * args:
 *    width - image width
 *    height - image height
 *
 * asserts:
 *    none
 *
 * returns: error code (0 - E_OK)
 */

int jpeg_init_decoder(int width, int height)
{
	if(jpeg_ctx != NULL)
		jpeg_close_decoder();

	jpeg_ctx = calloc(1, sizeof(jpeg_decoder_context_t));

	if(jpeg_ctx == NULL)
	{
		fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (jpeg_init_decoder): %s\n", strerror(errno));
		exit(-1);
	}
	
	jpeg_ctx->width = width;
	jpeg_ctx->height = height;
        jpeg_ctx->pic_size = width * height * 4;

	video_stream_info = malloc(sizeof(VideoStreamInfo));
	video_config = malloc(sizeof(VConfig));
	video_stream_data_info = malloc(sizeof(VideoStreamDataInfo));

	memset(video_stream_info, 0, sizeof(VideoStreamInfo));
	memset(video_stream_data_info, 0, sizeof(VideoStreamDataInfo));

	video_stream_info->eCodecFormat = VIDEO_CODEC_FORMAT_MJPEG;
	video_stream_info->nWidth = width;
	video_stream_info->nHeight = height;
	video_stream_info->nFrameRate = 30;
	video_stream_info->nFrameDuration = 33;

	video_config->eOutputPixelFormat = PIXEL_FORMAT_YV12;
	video_config->nVbvBufferSize = width * height * 4;

        video_decoder = CreateVideoDecoder();

        int result = InitializeVideoDecoder(video_decoder, video_stream_info, video_config);

	if (result != 0) {
		printf("Failed to initialize video decoder\n");
		return result;
	}

        result = RequestVideoStreamBuffer(video_decoder, 3 * width * height, &video_decoder_buffer, &video_decoder_buffer_size, &video_decoder_ring_buffer, &video_decoder_ring_buffer_size, 0);

        if (result != 0) {
            printf("Failed to RequestVideoStreamBuffer. result: %i \n", result);
            return result;
        }

	return E_OK;
}

/*
 * decode (m)jpeg frame
 * args:
 *    out_buf - pointer to decoded data
 *    in_buf - pointer to h264 data
 *    size - in_buf size
 *
 * asserts:
 *    jpeg_ctx is not null
 *    in_buf is not null
 *    out_buf is not null
 *
 * returns: decoded data size
 */
int jpeg_decode(uint8_t *out_buf, uint8_t *in_buf, int size)
{
	/*asserts*/
	assert(jpeg_ctx != NULL);
	assert(in_buf != NULL);
	assert(out_buf != NULL);

	int result = 0;

	video_stream_data_info->pData = in_buf;
	video_stream_data_info->nLength = size;
	video_stream_data_info->bIsFirstPart = 1;
	video_stream_data_info->bIsLastPart = 1;

	result = SubmitVideoStreamData(video_decoder, video_stream_data_info, 0);

	if (result != 0) {
		printf("Failed to send video data to decoder. result: %i \n", result);
		return result;
	}

	result = DecodeVideoStream(video_decoder, 0, 0, 1, 0);

	if (result == VDECODE_RESULT_NO_FRAME_BUFFER || result == VDECODE_RESULT_NO_BITSTREAM || result == VDECODE_RESULT_UNSUPPORTED) {
		printf("Failed to decode video. result: %i \n", result);
		return result;
	} else {
		result = 0;
	}

	VideoPicture *picture = RequestPicture(video_decoder, 0);

	if (picture == NULL) {
		printf("Failed to get picture. return null");
		return -1;
	}

	/*
	int width = jpeg_ctx->width;
	int height = jpeg_ctx->height;
	int pixIdx = 0;
	int rgbPixIdx = 0;

	for (int x=0; x<width; x++) {
	    for (int y=0; y<height; y++) {
		pixIdx = (x * width) + y;
		rgbPixIdx = pixIdx * 4;
		out_buf[rgbPixIdx] = 0; //picture->pData0[pixIdx];
	    }
	}*/

	printf("pData0 size %i \n", sizeof(picture->pData0));

	memcpy(out_buf, picture->pData0, jpeg_ctx->width * jpeg_ctx->height);

	result = ReturnPicture(video_decoder, picture);

	if (result != 0) {
		printf("Failed to return picture. result: %i \n", result);
	}

	return result;
}

/*
 * close (m)jpeg decoder context
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void jpeg_close_decoder()
{
	if(jpeg_ctx == NULL)
		return;
		
	free(jpeg_ctx);

	jpeg_ctx = NULL;

	DestroyVideoDecoder(video_decoder);
}

