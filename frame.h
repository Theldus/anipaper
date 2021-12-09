#ifndef FRAME_H
#define FRAME_H

	/**/
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
	};

	/**/
	extern void save_frame_ppm(AVFrame *frame,
		struct av_decode_params *dp);

#endif /* FRAME_H */
