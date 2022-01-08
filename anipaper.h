/*
 * MIT License
 *
 * Copyright (c) 2021-2022 Davidson Francis <davidsondfgl@gmail.com>
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

#ifndef ANIPAPER_H
#define ANIPAPER_H

	#include <SDL.h>

	/*
	 * Screen area (in %) that should be occupied to pause
	 * Anipaper.
	 */
#ifndef SCREEN_AREA_THRESHOLD
	#define SCREEN_AREA_THRESHOLD 70
#endif

	/* Check pause constant. */
#ifndef CHECK_PAUSE_MS
	#define CHECK_PAUSE_MS 100
#endif

	/* Logs. */
	#define LOG_GOTO(log,lbl) \
		do { \
			fprintf(stderr, "INFO: " log); \
			goto lbl; \
		} while (0)

	#define LOG(...) \
		fprintf(stderr, "INFO: " __VA_ARGS__)

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

		/* Pause stuff. */
		int paused;
		double time_before_pause;
		SDL_mutex *pause_mutex;
		SDL_cond *pause_cond;

		/* HW decoding. */
		AVBufferRef *hw_device_ctx;
		enum AVPixelFormat hw_pix_fmt;
	};

	extern void save_frame_ppm(AVFrame *frame,
		struct av_decode_params *dp);
	extern double time_secs(void);
	extern int screen_area_used(Display *disp, int screen_width,
		int screen_height);

#endif /* ANIPAPER_H */
