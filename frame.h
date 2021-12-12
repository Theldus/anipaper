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

#ifndef FRAME_H
#define FRAME_H

	/*
	 * Useful decode parameters, holds a bunch of data
	 * related to libav, screen, FPS management and so on.
	 */
	struct av_decode_params
	{
		/* Video decode stuff. */
		int video_idx;
		AVCodecContext *codec_context;
		AVFormatContext *format_context;

		/* Scale stuff. */
		struct SwsContext *sws_ctx;
		uint8_t *dst_img[4];
		int dst_linesize[4];
		int screen_width;
		int screen_height;

		/* FPS management. */
		double time_base;
		double frame_last_delay;
		double frame_last_pts;
		double frame_timer;
	};

	extern void save_frame_ppm(AVFrame *frame,
		struct av_decode_params *dp);

	extern int64_t time_microsecs(void);

#endif /* FRAME_H */
