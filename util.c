/*
 * MIT License
 *
 * Copyright (c) 2021 Davidson Francis <davidsondfgl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <sys/time.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include "frame.h"

/**
 * @brief Save the frame @p frame as a PPM file.
 * Made for debug purposes only.
 *
 * @param frame Frame to be saved.
 * @param dp av_decode_params structure.
 */
#ifdef DECODE_TO_FILE
void save_frame_ppm(AVFrame *frame, struct av_decode_params *dp)
{
	int i;
	FILE *f;
	char filename[64];

	/* Convert to RGB. */
	sws_scale(dp->sws_ctx, (const uint8_t * const*)frame->data,
			frame->linesize, 0, frame->height,
			dp->dst_img, dp->dst_linesize);

	/* Save file. */
	snprintf(filename, sizeof(filename), "out/frame_%04d.ppm",
		dp->codec_context->frame_number);

	f = fopen(filename, "wb");
	fprintf(f, "P6\n%d %d\n255\n", frame->width, frame->height);

	/* Write. */
	for (i = 0; i < frame->height; i++)
	{
		fwrite(dp->dst_img[0] + i * dp->dst_linesize[0], 1,
			frame->width * 3, f);
	}
	fclose(f);
}
#endif

/**
 * @brief Get the current time, in microseconds.
 *
 * @return Returns the current time.
 */
int64_t time_microsecs(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return ((int64_t)tv.tv_sec * 1000000 + tv.tv_usec);
}
