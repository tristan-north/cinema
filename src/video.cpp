extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswscale/swscale.h>
#include <libavutil/avstring.h>
#include <libavutil/time.h>


#define __RESAMPLER__
#define __LIBSWRESAMPLE__

#ifdef __RESAMPLER__
#include <libavutil/opt.h>

#ifdef __LIBAVRESAMPLE__
#include <libavresample/avresample.h>
#endif

#ifdef __LIBSWRESAMPLE__
#include <libswresample/swresample.h>
#endif
#endif
}

#include "video.h"
#include <SDL.h>
#include <SDL_thread.h>
#include <stdio.h>
#include <math.h>

#define SDL_AUDIO_BUFFER_SIZE 2048
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0

#define SAMPLE_CORRECTION_PERCENT_MAX 10
#define AUDIO_DIFF_AVG_NB 20

#define VIDEO_PICTURE_QUEUE_SIZE 2

#define DEFAULT_AV_SYNC_TYPE AV_SYNC_EXTERNAL_MASTER

typedef struct PacketQueue {
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
} PacketQueue;

typedef struct VideoPicture {
	unsigned char *bmp;
	int width, height; /* source height & width */
	int allocated;
	double pts;
} VideoPicture;

typedef struct VideoState {

	AVFormatContext *pFormatCtx;
	int             videoStream, audioStream;

	int             av_sync_type;
	double          external_clock; /* external clock base */
	int64_t         external_clock_time;

	double          audio_clock;
	AVStream        *audio_st;
	PacketQueue     audioq;
	AVFrame         audio_frame;
	uint8_t         audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	unsigned int    audio_buf_size;
	unsigned int    audio_buf_index;
	AVPacket        audio_pkt;
	uint8_t         *audio_pkt_data;
	int             audio_pkt_size;
	int             audio_hw_buf_size;
	double          audio_diff_cum; /* used for AV difference average computation */
	double          audio_diff_avg_coef;
	double          audio_diff_threshold;
	int             audio_diff_avg_count;
	uint8_t         audio_need_resample;
	double          frame_timer;
	double          frame_last_pts;
	double          frame_last_delay;
	double          video_clock; ///<pts of last decoded frame / predicted pts of next decoded frame
	double          video_current_pts; ///<current displayed pts (different from video_clock if frame fifos are used)
	int64_t         video_current_pts_time;  ///<time (av_gettime) at which we updated video_current_pts - used to have running video pts
	AVStream        *video_st;
	PacketQueue     videoq;

	VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int             pictq_size, pictq_rindex, pictq_windex;
	SDL_mutex       *pictq_mutex;
	SDL_cond        *pictq_cond;

	SDL_Thread      *parse_tid;
	SDL_Thread      *video_tid;

	char            filename[1024];

	AVIOContext     *io_context;
	struct SwsContext *sws_ctx;

#ifdef __RESAMPLER__
#ifdef __LIBAVRESAMPLE__
	AVAudioResampleContext *pSwrCtx;
#endif

#ifdef __LIBSWRESAMPLE__
	SwrContext *pSwrCtx;
#endif
	uint8_t *pResampledOut;
	int resample_lines;
	uint64_t resample_size;
#endif

} VideoState;

enum {
	AV_SYNC_AUDIO_MASTER,
	AV_SYNC_VIDEO_MASTER,
	AV_SYNC_EXTERNAL_MASTER,
};

SDL_Window     *window;
SDL_Renderer   *renderer;
unsigned char  *pixelBuffer;

extern bool g_running;

/* Since we only have one decoding thread, the Big Struct
   can be global in case we need it. */
VideoState *global_video_state;

uint64_t programStartTimeMs;

void packet_queue_init(PacketQueue *q) {
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

	AVPacketList *pkt1;

	if(av_dup_packet(pkt) < 0) {
		return -1;
	}

	pkt1 = (AVPacketList*)av_malloc(sizeof(AVPacketList));

	if(!pkt1) {
		return -1;
	}

	pkt1->pkt = *pkt;
	pkt1->next = NULL;

	SDL_LockMutex(q->mutex);

	if(!q->last_pkt) {
		q->first_pkt = pkt1;

	} else {
		q->last_pkt->next = pkt1;
	}

	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	SDL_CondSignal(q->cond);

	SDL_UnlockMutex(q->mutex);
	return 0;
}

static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
	AVPacketList *pkt1;
	int ret;

	SDL_LockMutex(q->mutex);

	for(;;) {

		if(!g_running) {
			ret = -1;
			break;
		}

		pkt1 = q->first_pkt;

		if(pkt1) {
			q->first_pkt = pkt1->next;

			if(!q->first_pkt) {
				q->last_pkt = NULL;
			}

			q->nb_packets--;
			q->size -= pkt1->pkt.size;
			*pkt = pkt1->pkt;
			av_free(pkt1);
			ret = 1;
			break;

		} else if(!block) {
			ret = 0;
			break;

		} else {
			SDL_CondWait(q->cond, q->mutex);
		}
	}

	SDL_UnlockMutex(q->mutex);
	return ret;
}

double get_audio_clock(VideoState *is) {
	double pts;
	int hw_buf_size, bytes_per_sec, n;

	pts = is->audio_clock; /* maintained in the audio thread */
	hw_buf_size = is->audio_buf_size - is->audio_buf_index;
	bytes_per_sec = 0;
	n = is->audio_st->codec->channels * 2;

	if(is->audio_st) {
		bytes_per_sec = is->audio_st->codec->sample_rate * n;
	}

	if(bytes_per_sec) {
		pts -= (double)hw_buf_size / bytes_per_sec;
	}

	return pts;
}

double get_video_clock(VideoState *is) {
	double delta;

	delta = (av_gettime() - is->video_current_pts_time) / 1000000.0;
	return is->video_current_pts + delta;
}

double get_external_clock(VideoState* /*is*/) {
	return av_gettime() / 1000000.0;
}

double get_master_clock(VideoState *is) {
	if(is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
		return get_video_clock(is);

	} else if(is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
		return get_audio_clock(is);

	} else {
		return get_external_clock(is);
	}
}

/* Add or subtract samples to get a better sync, return new
   audio buffer size */

int synchronize_audio(VideoState *is, short *samples,
					  int samples_size, double /*pts*/) {

	int n;
	double ref_clock;

	n = 2 * is->audio_st->codec->channels;

	if(is->av_sync_type != AV_SYNC_AUDIO_MASTER) {
		double diff, avg_diff;
		int wanted_size, min_size, max_size /*, nb_samples */;

		ref_clock = get_master_clock(is);
		diff = get_audio_clock(is) - ref_clock;

		if(diff < AV_NOSYNC_THRESHOLD) {
			// accumulate the diffs
			is->audio_diff_cum = diff + is->audio_diff_avg_coef
								 * is->audio_diff_cum;

			if(is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
				is->audio_diff_avg_count++;

			} else {
				avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

				if(fabs(avg_diff) >= is->audio_diff_threshold) {
					wanted_size = samples_size + ((int)(diff * is->audio_st->codec->sample_rate) * n);
					min_size = samples_size * ((100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100);
					max_size = samples_size * ((100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100);

					if(wanted_size < min_size) {
						wanted_size = min_size;

					} else if(wanted_size > max_size) {
						wanted_size = max_size;
					}

					if(wanted_size < samples_size) {
						/* remove samples */
						samples_size = wanted_size;

					} else if(wanted_size > samples_size) {
						uint8_t *samples_end, *q;
						int nb;

						/* add samples by copying final sample*/
						nb = (samples_size - wanted_size);
						samples_end = (uint8_t *)samples + samples_size - n;
						q = samples_end + n;

						while(nb > 0) {
							memcpy(q, samples_end, n);
							q += n;
							nb -= n;
						}

						samples_size = wanted_size;
					}
				}
			}

		} else {
			/* difference is TOO big; reset diff stuff */
			is->audio_diff_avg_count = 0;
			is->audio_diff_cum = 0;
		}
	}

	return samples_size;
}

long audio_tutorial_resample(VideoState *is, struct AVFrame *inframe) {

#ifdef __RESAMPLER__

#ifdef __LIBAVRESAMPLE__

// There is pre 1.0 libavresample and then there is above..
#if LIBAVRESAMPLE_VERSION_MAJOR == 0
	void **resample_input_bytes = (void **)inframe->extended_data;
#else
	uint8_t **resample_input_bytes = (uint8_t **)inframe->extended_data;
#endif

#else
	uint8_t **resample_input_bytes = (uint8_t **)inframe->extended_data;
#endif


	int resample_nblen = 0;
	long resample_long_bytes = 0;

	if( is->pResampledOut == NULL || inframe->nb_samples > is->resample_size) {
#if __LIBAVRESAMPLE__
		is->resample_size = av_rescale_rnd(avresample_get_delay(is->pSwrCtx) +
										   inframe->nb_samples,
										   is->audio_st->codec->sample_rate,
										   is->audio_st->codec->sample_rate,
										   AV_ROUND_UP);
#else
		is->resample_size = av_rescale_rnd(swr_get_delay(is->pSwrCtx,
										   is->audio_st->codec->sample_rate) +
										   inframe->nb_samples,
										   is->audio_st->codec->sample_rate,
										   is->audio_st->codec->sample_rate,
										   AV_ROUND_UP);
#endif

		if(is->pResampledOut != NULL) {
			av_free(is->pResampledOut);
			is->pResampledOut = NULL;
		}

		av_samples_alloc(&is->pResampledOut, &is->resample_lines, 2, int(is->resample_size),
						 AV_SAMPLE_FMT_S16, 0);

	}


#ifdef __LIBAVRESAMPLE__

// OLD API (0.0.3) ... still NEW API (1.0.0 and above).. very frustrating..
// USED IN FFMPEG 1.0 (LibAV SOMETHING!). New in FFMPEG 1.1 and libav 9
#if LIBAVRESAMPLE_VERSION_INT <= 3
	// AVResample OLD
	resample_nblen = avresample_convert(is->pSwrCtx, (void **)&is->pResampledOut, 0,
										is->resample_size,
										(void **)resample_input_bytes, 0, inframe->nb_samples);
#else
	//AVResample NEW
	resample_nblen = avresample_convert(is->pSwrCtx, (uint8_t **)&is->pResampledOut,
										0, is->resample_size,
										(uint8_t **)resample_input_bytes, 0, inframe->nb_samples);
#endif

#else
	// SWResample
	resample_nblen = swr_convert(is->pSwrCtx, (uint8_t **)&is->pResampledOut,
								 int(is->resample_size),
								 (const uint8_t **)resample_input_bytes, inframe->nb_samples);
#endif

	resample_long_bytes = av_samples_get_buffer_size(NULL, 2, resample_nblen,
						 AV_SAMPLE_FMT_S16, 1);

	if (resample_nblen < 0) {
		fprintf(stderr, "reSample to another sample format failed!\n");
		return -1;
	}

	return resample_long_bytes;

#else
	return -1;
#endif
}

int audio_decode_frame(VideoState *is, double *pts_ptr) {
	static bool finishedStream = false;
	if ( finishedStream ) return -1;

	/* For example with wma audio package size can be
	   like 100 000 bytes */
	long len1, data_size = 0;
	AVPacket *pkt = &is->audio_pkt;
	double pts;
	int n = 0;
#ifdef __RESAMPLER__
	long resample_size = 0;
#endif

	for(;;) {
		while(is->audio_pkt_size > 0) {
			int got_frame = 0;
			len1 = avcodec_decode_audio4(is->audio_st->codec, &is->audio_frame, &got_frame, pkt);

			if(len1 < 0) {
				/* if error, skip frame */
				is->audio_pkt_size = 0;
				break;
			}

			if(got_frame) {
				data_size =
					av_samples_get_buffer_size
					(
						NULL,
						is->audio_st->codec->channels,
						is->audio_frame.nb_samples,
						is->audio_st->codec->sample_fmt,
						1
					);

				if(data_size <= 0) {
					/* No data yet, get more frames */
					continue;
				}

#ifdef __RESAMPLER__

				if(is->audio_need_resample == 1) {
					resample_size = audio_tutorial_resample(is, &is->audio_frame);

					if( resample_size > 0 ) {
						memcpy(is->audio_buf, is->pResampledOut, resample_size);
						memset(is->pResampledOut, 0x00, resample_size);
					}

				} else {
#endif

					memcpy(is->audio_buf, is->audio_frame.data[0], data_size);
#ifdef __RESAMPLER__
				}

#endif
			}

			is->audio_pkt_data += len1;
			is->audio_pkt_size -= len1;

			pts = is->audio_clock;
			*pts_ptr = pts;
			n = 2 * is->audio_st->codec->channels;

#ifdef __RESAMPLER__

			/* If you just return original data_size you will suffer
			   for clicks because you don't have that much data in
			   queue incoming so return resampled size. */
			if(is->audio_need_resample == 1) {
				is->audio_clock += (double)resample_size /
								   (double)(n * is->audio_st->codec->sample_rate);
				return resample_size;

			} else {
#endif
				/* We have data, return it and come back for more later */
				is->audio_clock += (double)data_size /
								   (double)(n * is->audio_st->codec->sample_rate);
				return data_size;
#ifdef __RESAMPLER__
			}

#endif

		}

		if(pkt->data) {
			av_free_packet(pkt);
		}

		if(!g_running) {
			return -1;
		}

		/* next packet */
		if(packet_queue_get(&is->audioq, pkt, 1) < 0) {
			return -1;
		}
		if( pkt->duration == 0 ) {
			finishedStream = true;
			return -1;
		}

		is->audio_pkt_data = pkt->data;
		is->audio_pkt_size = pkt->size;

		/* if update, update the audio clock w/pts */
		if(pkt->pts != AV_NOPTS_VALUE) {
			is->audio_clock = av_q2d(is->audio_st->time_base) * pkt->pts;
		}
	}
}

void audio_callback(void *userdata, Uint8 *stream, int len) {

	VideoState *is = (VideoState *)userdata;
	long len1, audio_size;
	double pts;

	while(len > 0) {

		if(is->audio_buf_index >= is->audio_buf_size) {
			/* We have already sent all our data; get more */
			audio_size = audio_decode_frame(is, &pts);

			if(audio_size < 0) {
				/* If error, output silence */
				is->audio_buf_size = 1024;
				memset(is->audio_buf, 0, is->audio_buf_size);
			} else {
				audio_size = synchronize_audio(is, (int16_t *)is->audio_buf,
											   audio_size, pts);
				is->audio_buf_size = audio_size;
			}

			is->audio_buf_index = 0;
		}

		len1 = is->audio_buf_size - is->audio_buf_index;

		if(len1 > len) {
			len1 = len;
		}

		memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
		len -= len1;
		stream += len1;
		is->audio_buf_index += len1;
	}

}

static Uint32 sdl_refresh_timer_cb(Uint32 /*interval*/, void *opaque) {
	SDL_Event event;
	event.type = FF_REFRESH_EVENT;
	event.user.data1 = opaque;
	SDL_PushEvent(&event);
	return 0; // 0 means stop timer
}

/* schedule a video refresh in 'delay' ms */
static void schedule_refresh(VideoState *is, int delay) {
	SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}


// This does some sync-ing stuff, copies the image into pixelBuffer,
// decrements the picture queue size
// and lets queue_picture() know that there's a free spot in the queue.
//
// This gets called in the main thread after an FF_REFRESH_EVENT.
void video_refresh_timer(void *userdata) {

	VideoState *is = (VideoState *)userdata;
	VideoPicture *vp;
	double actual_delay, delay, sync_threshold, ref_clock, diff;

	if(is->video_st) {
		if(is->pictq_size == 0) {
			schedule_refresh(is, 10);

		} else {
			vp = &is->pictq[is->pictq_rindex];

			is->video_current_pts = vp->pts;
			is->video_current_pts_time = av_gettime();

			delay = vp->pts - is->frame_last_pts; /* the pts from last time */

			if(delay <= 0 || delay >= 1.0) {
				/* if incorrect delay, use previous one */
				delay = is->frame_last_delay;
			}

			/* save for next time */
			is->frame_last_delay = delay;
			is->frame_last_pts = vp->pts;

			/* update delay to sync to audio if not master source */
			if(is->av_sync_type != AV_SYNC_VIDEO_MASTER) {
				ref_clock = get_master_clock(is);
				diff = vp->pts - ref_clock;

				/* Skip or repeat the frame. Take delay into account
				   FFPlay still doesn't "know if this is the best guess." */
				sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;

				if(fabs(diff) < AV_NOSYNC_THRESHOLD) {
					if(diff <= -sync_threshold) {
						delay = 0;

					} else if(diff >= sync_threshold) {
						delay = 2 * delay;
					}
				}
			}

			is->frame_timer += delay;
			/* computer the REAL delay */
			actual_delay = is->frame_timer - (av_gettime() / 1000000.0);

			if(actual_delay < 0.010) {
				/* Really it should skip the picture instead */
				actual_delay = 0.010;
			}

			// Since the callback tends to happen a bit later than requested,
			// add a 5ms fudge factor.
			const int fudge = -5;
			schedule_refresh(is, (int)(actual_delay * 1000 + 0.5 + fudge));
			/* show the picture! */
			memcpy(pixelBuffer, is->pictq[is->pictq_rindex].bmp, vp->width* vp->height*4);

			/* update queue for next picture! */
			if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
				is->pictq_rindex = 0;
			}

			SDL_LockMutex(is->pictq_mutex);
			is->pictq_size--;
			SDL_CondSignal(is->pictq_cond);
			SDL_UnlockMutex(is->pictq_mutex);
		}

	} else {
		schedule_refresh(is, 100);
	}
}

void alloc_picture(VideoState *is) {

	VideoPicture *vp;

	for(size_t i = 0; i < VIDEO_PICTURE_QUEUE_SIZE; i++) {
		vp = &is->pictq[i];
		vp->width = is->video_st->codec->width;
		vp->height = is->video_st->codec->height;
		vp->bmp = (unsigned char*)malloc(vp->width * vp->height * 4);
		vp->allocated = 1;
	}

	pixelBuffer = (unsigned char*)malloc(vp->width * vp->height * 4);
}


// This waits until the picture queue isn't full then
// copies the video frame into the texture.
// It somehow lets the display thread know that there is
// a picture ready via is->pictq_windex.
int queue_picture(VideoState *is, AVFrame *pFrame, double pts) {
	VideoPicture *vp;
	AVPicture pict;

	/* wait until we have space for a new pic */
	SDL_LockMutex(is->pictq_mutex);
	while(is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE && g_running) {
		SDL_CondWait(is->pictq_cond, is->pictq_mutex);
	}
	SDL_UnlockMutex(is->pictq_mutex);

	if(!g_running)
		return -1;

	// windex vidState set to 0 initially
	vp = &is->pictq[is->pictq_windex];

	/* We have a place to put our picture on the queue */
	/* If we are skipping a frame, do we set this to null
	 but still return vp->allocated = 1? */


	// Get frame pixels into pict.data
	int w = is->video_st->codec->width;
	int h = is->video_st->codec->height;

	avpicture_fill(&pict, vp->bmp, AV_PIX_FMT_BGRA, w, h);

	sws_scale(is->sws_ctx, (const uint8_t * const *)pFrame->data,
			  pFrame->linesize, 0, h,
			  pict.data, pict.linesize);

	vp->pts = pts;


	// now we inform our display thread that we have a pic ready
	if(++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
		is->pictq_windex = 0;
	}
	SDL_LockMutex(is->pictq_mutex);
	is->pictq_size++;
	SDL_UnlockMutex(is->pictq_mutex);

	return 0;
}

double synchronize_video(VideoState *is, AVFrame *src_frame, double pts) {

	double frame_delay;

	if(pts != 0) {
		/* if we have pts, set video clock to it */
		is->video_clock = pts;

	} else {
		/* if we aren't given a pts, set it to the clock */
		pts = is->video_clock;
	}

	/* update the video clock */
	frame_delay = av_q2d(is->video_st->codec->time_base);
	/* if we are repeating a frame, adjust clock accordingly */
	frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
	is->video_clock += frame_delay;
	return pts;
}

uint64_t global_video_pkt_pts = AV_NOPTS_VALUE;

/* These are called whenever we allocate a frame
 * buffer. We use this to store the global_pts in
 * a frame at the time it is allocated.
 */

int our_get_buffer(struct AVCodecContext *c, AVFrame *pic, int flags) {
	int ret = avcodec_default_get_buffer2(c, pic, flags);
	uint64_t *pts = (uint64_t*)av_malloc(sizeof(uint64_t));
	*pts = global_video_pkt_pts;
	pic->opaque = pts;
	return ret;
}


// This thread does the video decoding. It has an infinite loop where
// it gets packets from the video queue, decodes them and puts them
// on the picture queue.
//
// It gets started from stream_component_open().
int video_thread(void *arg) {
	VideoState *is = (VideoState *)arg;
	AVPacket pkt1, *packet = &pkt1;
	int frameFinished;
	AVFrame *pFrame;
	double pts;

	pFrame = av_frame_alloc();

	for(;;) {
		if(packet_queue_get(&is->videoq, packet, 1) < 0) {
			// means we quit getting packets
			break;
		}

		pts = 0;

		// Save global pts to be stored in pFrame in first call
		global_video_pkt_pts = packet->pts;
		// Decode video frame
		avcodec_decode_video2(is->video_st->codec, pFrame, &frameFinished,
							  packet);

		if(packet->dts == AV_NOPTS_VALUE
				&& pFrame->opaque && *(uint64_t*)pFrame->opaque != AV_NOPTS_VALUE) {
			pts = double(*(uint64_t *)pFrame->opaque);

		} else if(packet->dts != AV_NOPTS_VALUE) {
//			pts = double(packet->dts);
			pts = double(av_frame_get_best_effort_timestamp(pFrame));

		} else {
			pts = 0;
		}

		pts *= av_q2d(is->video_st->time_base);

		// Did we get a video frame?
		if(frameFinished) {
			pts = synchronize_video(is, pFrame, pts);

			if(queue_picture(is, pFrame, pts) < 0) {
				break;
			}
		}

		av_free_packet(packet);
	}
	av_free(pFrame);
	return 0;
}


// This gets called once for the audio stream and once for the video stream.
// It sorts out codecs, starts SDL audio stuff and starts video_thread which
// does the actual decoding of packets on the video queue.
//
// It gets called from decode_thread which gets started from main.
int stream_component_open(VideoState *is, int stream_index) {
	AVFormatContext *pFormatCtx = is->pFormatCtx;
	AVCodecContext *codecCtx = NULL;
	AVCodec *codec = NULL;
	AVDictionary *optionsDict = NULL;
	SDL_AudioSpec wanted_spec, spec;

	if(stream_index < 0 || stream_index >= int(pFormatCtx->nb_streams)) {
		return -1;
	}

	// Get a pointer to the codec context for the video stream
	codecCtx = pFormatCtx->streams[stream_index]->codec;

	if(codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
		// Set audio settings from codec info
		wanted_spec.freq = codecCtx->sample_rate; // Resampling to this
		wanted_spec.format = AUDIO_S16SYS;
		wanted_spec.channels = 2;  // Resampling to this
		wanted_spec.silence = 0;
		wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
		wanted_spec.callback = audio_callback;
		wanted_spec.userdata = is;

		if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
			fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
			return -1;
		}

		is->audio_hw_buf_size = spec.size;
	}

	codec = avcodec_find_decoder(codecCtx->codec_id);

	if(!codec || (avcodec_open2(codecCtx, codec, &optionsDict) < 0)) {
		fprintf(stderr, "Unsupported codec!\n");
		return -1;
	}

	switch(codecCtx->codec_type) {
		case AVMEDIA_TYPE_AUDIO:
			is->audioStream = stream_index;
			is->audio_st = pFormatCtx->streams[stream_index];
			is->audio_buf_size = 0;
			is->audio_buf_index = 0;

			// averaging filter for audio sync
			is->audio_diff_avg_coef = exp(log(0.01 / AUDIO_DIFF_AVG_NB));
			is->audio_diff_avg_count = 0;
			// Correct audio only if larger error than this
			is->audio_diff_threshold = 2.0 * SDL_AUDIO_BUFFER_SIZE / codecCtx->sample_rate;

			memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
			packet_queue_init(&is->audioq);

			SDL_PauseAudio(0);
			break;

		case AVMEDIA_TYPE_VIDEO:
			is->videoStream = stream_index;
			is->video_st = pFormatCtx->streams[stream_index];

			is->frame_timer = (double)av_gettime() / 1000000.0;
			is->frame_last_delay = 40e-3;
			is->video_current_pts_time = av_gettime();

			packet_queue_init(&is->videoq);
			is->video_tid = SDL_CreateThread(video_thread, "video_thread", is);
			is->sws_ctx =
				sws_getContext
				(
					is->video_st->codec->width,
					is->video_st->codec->height,
					is->video_st->codec->pix_fmt,
					is->video_st->codec->width,
					is->video_st->codec->height,
					AV_PIX_FMT_RGBA,
					SWS_BILINEAR,
					NULL,
					NULL,
					NULL
				);
			codecCtx->get_buffer2 = our_get_buffer;

			break;

		default:
			break;
	}

	return 0;
}


// The ffmpeg functions which block routinely call this
// function to see if they should quit or not.
int decode_interrupt_cb(void* /*opaque*/) {
	return (global_video_state && (!g_running));
}



// This thread opens the file. It finds the audio and video
// streams, calls stream_component_open() on them, sets up
// resampling. Then it runs in a loop reading the packets
// and putting them on either the audio or video packet queue.
//
// It gets started from main.
int decode_thread(void *arg) {
	VideoState *is = (VideoState *)arg;
	AVPacket pkt1, *packet = &pkt1;

	// main decode loop
	for(;;) {
		if(!g_running) {
			break;
		}

		// seek stuff goes here
		if(is->audioq.size > MAX_AUDIOQ_SIZE ||
				is->videoq.size > MAX_VIDEOQ_SIZE) {
			SDL_Delay(10);
			continue;
		}

		if(av_read_frame(is->pFormatCtx, packet) < 0) {
			if(is->pFormatCtx->pb->error == 0) {
				// If end of stream, put packet with duration 0 on the audio queue
				// to signal EOF to packet decode function.
				packet->duration = 0;
				packet_queue_put(&is->audioq, packet);
				break;
			}
		}

		// Is this a packet from the video stream?
		if(packet->stream_index == is->videoStream) {
			packet_queue_put(&is->videoq, packet);

		} else if(packet->stream_index == is->audioStream) {
			packet_queue_put(&is->audioq, packet);

		} else {
			av_free_packet(packet);
		}
	}

	printf("Decode thread finished.\n");

	return 0;
}

int video_initialize(const char *filepath) {

	programStartTimeMs = av_gettime()/1000;

	VideoState *is;

	is = (VideoState*)av_mallocz(sizeof(VideoState));

	av_register_all();

	strncpy_s(is->filename, filepath, 1024);

	is->pictq_mutex = SDL_CreateMutex();
	is->pictq_cond = SDL_CreateCond();

	schedule_refresh(is, 40);

	is->av_sync_type = DEFAULT_AV_SYNC_TYPE;

	AVFormatContext *pFormatCtx = NULL;

	AVDictionary *io_dict = NULL;
	AVIOInterruptCB callback;

	int video_index = -1;
	int audio_index = -1;
	int i;

	is->videoStream = -1;
	is->audioStream = -1;
	is->audio_need_resample = 0;

	global_video_state = is;
	// will interrupt blocking functions if we quit!
	callback.callback = decode_interrupt_cb;
	callback.opaque = is;

	if(avio_open2(&is->io_context, is->filename, 0, &callback, &io_dict)) {
		fprintf(stderr, "Unable to open I/O for %s\n", is->filename);
		return -1;
	}

	// Open video file
	if(avformat_open_input(&pFormatCtx, is->filename, NULL, NULL) != 0) {
		return -1;    // Couldn't open file
	}

	is->pFormatCtx = pFormatCtx;

	// Retrieve stream information
	if(avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		return -1;    // Couldn't find stream information
	}

	// Dump information about file onto standard error
	fprintf(stderr, "===============================================================================\n");
	av_dump_format(pFormatCtx, 0, is->filename, 0);
	fprintf(stderr, "===============================================================================\n");

	// Find the first video and audio stream
	for(i = 0; i < int(pFormatCtx->nb_streams); i++) {
		if(pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
				video_index < 0) {
			video_index = i;
		}

		if(pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
				audio_index < 0) {
			audio_index = i;
		}
	}

	if(audio_index >= 0) {
		stream_component_open(is, audio_index);
	}

	if(video_index >= 0) {
		stream_component_open(is, video_index);
	}

	if(is->videoStream < 0 && is->audioStream < 0) {
		fprintf(stderr, "%s: could not open codecs\n", is->filename);
		return -1;
	}

#ifdef __RESAMPLER__

//	if( audio_index >= 0
//			&& pFormatCtx->streams[audio_index]->codec->sample_fmt != AV_SAMPLE_FMT_S16) {
		is->audio_need_resample = 1;
		is->pResampledOut = NULL;
		is->pSwrCtx = NULL;

		printf("Configure resampler: ");

#ifdef __LIBAVRESAMPLE__
		printf("libAvResample\n");
		is->pSwrCtx = avresample_alloc_context();
#endif

#ifdef __LIBSWRESAMPLE__
		printf("libSwResample\n");
		is->pSwrCtx = swr_alloc();
#endif

		// Some MP3/WAV don't tell this so make assumtion that
		// They are stereo not 5.1
		if (pFormatCtx->streams[audio_index]->codec->channel_layout == 0
				&& pFormatCtx->streams[audio_index]->codec->channels == 2) {
			pFormatCtx->streams[audio_index]->codec->channel_layout = AV_CH_LAYOUT_STEREO;

		} else if (pFormatCtx->streams[audio_index]->codec->channel_layout == 0
				   && pFormatCtx->streams[audio_index]->codec->channels == 1) {
			pFormatCtx->streams[audio_index]->codec->channel_layout = AV_CH_LAYOUT_MONO;

		} else if (pFormatCtx->streams[audio_index]->codec->channel_layout == 0
				   && pFormatCtx->streams[audio_index]->codec->channels == 0) {
			pFormatCtx->streams[audio_index]->codec->channel_layout = AV_CH_LAYOUT_STEREO;
			pFormatCtx->streams[audio_index]->codec->channels = 2;
		}

		av_opt_set_int(is->pSwrCtx, "in_channel_layout",
					   pFormatCtx->streams[audio_index]->codec->channel_layout, 0);
		av_opt_set_int(is->pSwrCtx, "in_sample_fmt",
					   pFormatCtx->streams[audio_index]->codec->sample_fmt, 0);
		av_opt_set_int(is->pSwrCtx, "in_sample_rate",
					   pFormatCtx->streams[audio_index]->codec->sample_rate, 0);

		printf("channel layout: %d\n", pFormatCtx->streams[audio_index]->codec->channel_layout);
		av_opt_set_int(is->pSwrCtx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
		av_opt_set_int(is->pSwrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
		av_opt_set_int(is->pSwrCtx, "out_sample_rate",  pFormatCtx->streams[audio_index]->codec->sample_rate, 0);

#ifdef __LIBAVRESAMPLE__

		if (avresample_open(is->pSwrCtx) < 0) {
#else

		if (swr_init(is->pSwrCtx) < 0) {
#endif
			fprintf(stderr, " ERROR!! From Samplert: %d Hz Sample format: %s\n",
					pFormatCtx->streams[audio_index]->codec->sample_rate,
					av_get_sample_fmt_name(pFormatCtx->streams[audio_index]->codec->sample_fmt));
			fprintf(stderr, "         To Sample format: s16\n");
			is->audio_need_resample = 0;
			is->pSwrCtx = NULL;;
		}

//	}

#endif

		alloc_picture(is);

		is->parse_tid = SDL_CreateThread(decode_thread, "decode_thread", is);
		if(!is->parse_tid) {
			av_free(is);
			return -1;
		}

	return 0;

}


unsigned char * video_get_frame_pixels() {
	if(pixelBuffer)
		return pixelBuffer;
	else
		return 0;
}

int video_get_width() {
	VideoPicture *vp = &global_video_state->pictq[global_video_state->pictq_rindex];

	return vp->width;
}

int video_get_height() {
	VideoPicture *vp = &global_video_state->pictq[global_video_state->pictq_rindex];

	return vp->height;
}


void video_shutdown()
{
	VideoPicture *vp;
	for(size_t i = 0; i < VIDEO_PICTURE_QUEUE_SIZE; i++) {
		vp = &global_video_state->pictq[i];
		free(vp->bmp);
	}

	SDL_CondSignal(global_video_state->audioq.cond);
	SDL_CondSignal(global_video_state->videoq.cond);
}
