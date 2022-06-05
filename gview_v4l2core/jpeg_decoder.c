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
#include "jpeg_dec_main.h"

extern int verbosity;

typedef struct _jpeg_decoder_context_t
{
	int width;
	int height;
	int pic_size;
	
} jpeg_decoder_context_t;

static jpeg_decoder_context_t *jpeg_ctx = NULL;
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
	printf("jpeg_init_decoder\n");
	fflush(stdout);

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

	hw_init(width, height);

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

	hw_decode_jpeg_main(in_buf, size, out_buf);

	return 0;
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

	hw_close();

}

