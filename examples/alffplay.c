/*
 * alffplay.c
 *
 * A pedagogical video player that really works! Now with seeking features.
 *
 * Code based on FFplay, Copyright (c) 2003 Fabrice Bellard, and a tutorial by
 * Martin Bohme <boehme@inb.uni-luebeckREMOVETHIS.de>.
 *
 * Requires C99.
 */

#include <stdio.h>
#include <math.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/time.h>
#include <libavutil/avstring.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#include <SDL.h>
#include <SDL_thread.h>
#include <SDL_video.h>

#include "threads.h"
#include "bool.h"

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"


static bool has_latency_check = false;
static LPALGETSOURCEDVSOFT alGetSourcedvSOFT;

#define AUDIO_BUFFER_TIME 100 /* In milliseconds, per-buffer */
#define AUDIO_BUFFER_QUEUE_SIZE 8 /* Number of buffers to queue */
#define MAX_AUDIOQ_SIZE (5 * 16 * 1024) /* Bytes of compressed audio data to keep queued */
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024) /* Bytes of compressed video data to keep queued */
#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0
#define SAMPLE_CORRECTION_MAX_DIFF 0.1
#define AUDIO_DIFF_AVG_NB 20
#define VIDEO_PICTURE_QUEUE_SIZE 16

enum {
    FF_UPDATE_EVENT = SDL_USEREVENT,
    FF_REFRESH_EVENT,
    FF_QUIT_EVENT
};


typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    volatile int nb_packets;
    volatile int size;
    volatile bool flushing;
    almtx_t mutex;
    alcnd_t cond;
} PacketQueue;

typedef struct VideoPicture {
    SDL_Texture *bmp;
    int width, height; /* Logical image size (actual size may be larger) */
    volatile bool updated;
    double pts;
} VideoPicture;

typedef struct AudioState {
    AVStream *st;

    PacketQueue q;
    AVPacket    pkt;

    /* Used for clock difference average computation */
    double diff_accum;
    double diff_avg_coef;
    double diff_threshold;

    /* Time (in seconds) of the next sample to be buffered */
    double current_pts;

    /* Decompressed sample frame, and swresample context for conversion */
    AVFrame           *decoded_aframe;
    struct SwrContext *swres_ctx;

    /* Conversion format, for what gets fed to OpenAL */
    int                 dst_ch_layout;
    enum AVSampleFormat dst_sample_fmt;

    /* Storage of converted samples */
    uint8_t *samples;
    ssize_t samples_len; /* In samples */
    ssize_t samples_pos;
    int     samples_max;

    /* OpenAL format */
    ALenum  format;
    ALint   frame_size;

    ALuint  source;
    ALuint  buffer[AUDIO_BUFFER_QUEUE_SIZE];
    ALuint  buffer_idx;
    almtx_t src_mutex;

    althrd_t thread;
} AudioState;

typedef struct VideoState {
    AVStream *st;

    PacketQueue q;

    double  clock;
    double  frame_timer;
    double  frame_last_pts;
    double  frame_last_delay;
    double  current_pts;
    /* time (av_gettime) at which we updated current_pts - used to have running video pts */
    int64_t current_pts_time;

    /* Decompressed video frame, and swscale context for conversion */
    AVFrame           *decoded_vframe;
    struct SwsContext *swscale_ctx;

    VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
    int          pictq_size, pictq_rindex, pictq_windex;
    almtx_t      pictq_mutex;
    alcnd_t      pictq_cond;

    althrd_t thread;
} VideoState;

typedef struct MovieState {
    AVFormatContext *pFormatCtx;
    int             videoStream, audioStream;

    volatile bool seek_req;
    int64_t       seek_pos;

    int av_sync_type;

    int64_t external_clock_base;

    AudioState audio;
    VideoState video;

    althrd_t parse_thread;

    char filename[1024];

    volatile bool quit;
} MovieState;

enum {
    AV_SYNC_AUDIO_MASTER,
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_MASTER,

    DEFAULT_AV_SYNC_TYPE = AV_SYNC_EXTERNAL_MASTER
};

static AVPacket flush_pkt = { .data = (uint8_t*)"FLUSH" };

static void packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    almtx_init(&q->mutex, almtx_plain);
    alcnd_init(&q->cond);
}
static int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    AVPacketList *pkt1;
    if(pkt != &flush_pkt && !pkt->buf && av_dup_packet(pkt) < 0)
        return -1;

    pkt1 = av_malloc(sizeof(AVPacketList));
    if(!pkt1) return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    almtx_lock(&q->mutex);
    if(!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    almtx_unlock(&q->mutex);

    alcnd_signal(&q->cond);
    return 0;
}
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, MovieState *state)
{
    AVPacketList *pkt1;
    int ret = -1;

    almtx_lock(&q->mutex);
    while(!state->quit)
    {
        pkt1 = q->first_pkt;
        if(pkt1)
        {
            q->first_pkt = pkt1->next;
            if(!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        }

        if(q->flushing)
        {
            ret = 0;
            break;
        }
        alcnd_wait(&q->cond, &q->mutex);
    }
    almtx_unlock(&q->mutex);
    return ret;
}
static void packet_queue_clear(PacketQueue *q)
{
    AVPacketList *pkt, *pkt1;

    almtx_lock(&q->mutex);
    for(pkt = q->first_pkt;pkt != NULL;pkt = pkt1)
    {
        pkt1 = pkt->next;
        if(pkt->pkt.data != flush_pkt.data)
            av_free_packet(&pkt->pkt);
        av_freep(&pkt);
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    almtx_unlock(&q->mutex);
}
static void packet_queue_flush(PacketQueue *q)
{
    almtx_lock(&q->mutex);
    q->flushing = true;
    almtx_unlock(&q->mutex);
    alcnd_signal(&q->cond);
}
static void packet_queue_deinit(PacketQueue *q)
{
    packet_queue_clear(q);
    alcnd_destroy(&q->cond);
    almtx_destroy(&q->mutex);
}


static double get_audio_clock(AudioState *state)
{
    double pts;

    almtx_lock(&state->src_mutex);
    /* The audio clock is the timestamp of the sample currently being heard.
     * It's based on 4 components:
     * 1 - The timestamp of the next sample to buffer (state->current_pts)
     * 2 - The length of the source's buffer queue (AL_SEC_LENGTH_SOFT)
     * 3 - The offset OpenAL is currently at in the source (the first value
     *     from AL_SEC_OFFSET_LATENCY_SOFT)
     * 4 - The latency between OpenAL and the DAC (the second value from
     *     AL_SEC_OFFSET_LATENCY_SOFT)
     *
     * Subtracting the length of the source queue from the next sample's
     * timestamp gives the timestamp of the sample at start of the source
     * queue. Adding the source offset to that results in the timestamp for
     * OpenAL's current position, and subtracting the source latency from that
     * gives the timestamp of the sample currently at the DAC.
     */
    pts = state->current_pts;
    if(state->source)
    {
        ALdouble offset[2] = { 0.0, 0.0 };
        ALdouble queue_len = 0.0;
        ALint status;

        /* NOTE: The source state must be checked last, in case an underrun
         * occurs and the source stops between retrieving the offset+latency
         * and getting the state. */
        if(has_latency_check)
        {
            alGetSourcedvSOFT(state->source, AL_SEC_OFFSET_LATENCY_SOFT, offset);
            alGetSourcedvSOFT(state->source, AL_SEC_LENGTH_SOFT, &queue_len);
        }
        else
        {
            ALint ioffset, ilen;
            alGetSourcei(state->source, AL_SAMPLE_OFFSET, &ioffset);
            alGetSourcei(state->source, AL_SAMPLE_LENGTH_SOFT, &ilen);
            offset[0] = (double)ioffset / state->st->codec->sample_rate;
            queue_len = (double)ilen / state->st->codec->sample_rate;
        }
        alGetSourcei(state->source, AL_SOURCE_STATE, &status);

        /* If the source is AL_STOPPED, then there was an underrun and all
         * buffers are processed, so ignore the source queue. The audio thread
         * will put the source into an AL_INITIAL state and clear the queue
         * when it starts recovery. */
        if(status != AL_STOPPED)
            pts = pts - queue_len + offset[0];
        if(status == AL_PLAYING)
            pts = pts - offset[1];
    }
    almtx_unlock(&state->src_mutex);

    return (pts >= 0.0) ? pts : 0.0;
}
static double get_video_clock(VideoState *state)
{
    double delta = (av_gettime() - state->current_pts_time) / 1000000.0;
    return state->current_pts + delta;
}
static double get_external_clock(MovieState *movState)
{
    return (av_gettime()-movState->external_clock_base) / 1000000.0;
}

double get_master_clock(MovieState *movState)
{
    if(movState->av_sync_type == AV_SYNC_VIDEO_MASTER)
        return get_video_clock(&movState->video);
    if(movState->av_sync_type == AV_SYNC_AUDIO_MASTER)
        return get_audio_clock(&movState->audio);
    return get_external_clock(movState);
}

/* Return how many samples to skip to maintain sync (negative means to
 * duplicate samples). */
static int synchronize_audio(MovieState *movState)
{
    double diff, avg_diff;
    double ref_clock;

    if(movState->av_sync_type == AV_SYNC_AUDIO_MASTER)
        return 0;

    ref_clock = get_master_clock(movState);
    diff = ref_clock - get_audio_clock(&movState->audio);

    if(!(diff < AV_NOSYNC_THRESHOLD))
    {
        /* Difference is TOO big; reset diff stuff */
        movState->audio.diff_accum = 0.0;
        return 0;
    }

    /* Accumulate the diffs */
    movState->audio.diff_accum = movState->audio.diff_accum*movState->audio.diff_avg_coef + diff;
    avg_diff = movState->audio.diff_accum*(1.0 - movState->audio.diff_avg_coef);
    if(fabs(avg_diff) < movState->audio.diff_threshold)
        return 0;

    /* Constrain the per-update difference to avoid exceedingly large skips */
    if(!(diff <= SAMPLE_CORRECTION_MAX_DIFF))
        diff = SAMPLE_CORRECTION_MAX_DIFF;
    else if(!(diff >= -SAMPLE_CORRECTION_MAX_DIFF))
        diff = -SAMPLE_CORRECTION_MAX_DIFF;
    return (int)(diff*movState->audio.st->codec->sample_rate);
}

static int audio_decode_frame(MovieState *movState)
{
    AVPacket *pkt = &movState->audio.pkt;

    while(!movState->quit)
    {
        while(!movState->quit && pkt->size == 0)
        {
            av_free_packet(pkt);

            /* Get the next packet */
            int err;
            if((err=packet_queue_get(&movState->audio.q, pkt, movState)) <= 0)
            {
                if(err == 0)
                    break;
                return err;
            }
            if(pkt->data == flush_pkt.data)
            {
                avcodec_flush_buffers(movState->audio.st->codec);
                movState->audio.diff_accum = 0.0;
                movState->audio.current_pts = av_q2d(movState->audio.st->time_base)*pkt->pts;

                alSourceRewind(movState->audio.source);
                alSourcei(movState->audio.source, AL_BUFFER, 0);

                av_new_packet(pkt, 0);

                return -1;
            }

            /* If provided, update w/ pts */
            if(pkt->pts != AV_NOPTS_VALUE)
                movState->audio.current_pts = av_q2d(movState->audio.st->time_base)*pkt->pts;
        }

        AVFrame *frame = movState->audio.decoded_aframe;
        int got_frame = 0;
        int len1 = avcodec_decode_audio4(movState->audio.st->codec, frame,
                                         &got_frame, pkt);
        if(len1 < 0) break;

        if(len1 <= pkt->size)
        {
            /* Move the unread data to the front and clear the end bits */
            int remaining = pkt->size - len1;
            memmove(pkt->data, &pkt->data[len1], remaining);
            av_shrink_packet(pkt, remaining);
        }

        if(!got_frame || frame->nb_samples <= 0)
        {
            av_frame_unref(frame);
            continue;
        }

        if(frame->nb_samples > movState->audio.samples_max)
        {
            av_freep(&movState->audio.samples);
            av_samples_alloc(
                &movState->audio.samples, NULL, movState->audio.st->codec->channels,
                frame->nb_samples, movState->audio.dst_sample_fmt, 0
            );
            movState->audio.samples_max = frame->nb_samples;
        }
        /* Return the amount of sample frames converted */
        int data_size = swr_convert(movState->audio.swres_ctx,
            &movState->audio.samples, frame->nb_samples,
            (const uint8_t**)frame->data, frame->nb_samples
        );

        av_frame_unref(frame);
        return data_size;
    }

    return -1;
}

static int read_audio(MovieState *movState, uint8_t *samples, int length)
{
    int sample_skip = synchronize_audio(movState);
    int audio_size = 0;

    /* Read the next chunk of data, refill the buffer, and queue it
     * on the source */
    length /= movState->audio.frame_size;
    while(audio_size < length)
    {
        if(movState->audio.samples_len <= 0 || movState->audio.samples_pos >= movState->audio.samples_len)
        {
            int frame_len = audio_decode_frame(movState);
            if(frame_len < 0) return -1;

            movState->audio.samples_len = frame_len;
            if(movState->audio.samples_len == 0)
                break;

            movState->audio.samples_pos = (movState->audio.samples_len < sample_skip) ?
                                          movState->audio.samples_len : sample_skip;
            sample_skip -= movState->audio.samples_pos;

            movState->audio.current_pts += (double)movState->audio.samples_pos /
                                           (double)movState->audio.st->codec->sample_rate;
            continue;
        }

        int rem = length - audio_size;
        if(movState->audio.samples_pos >= 0)
        {
            int n = movState->audio.frame_size;
            int len = movState->audio.samples_len - movState->audio.samples_pos;
            if(rem > len) rem = len;
            memcpy(samples + audio_size*n,
                   movState->audio.samples + movState->audio.samples_pos*n,
                   rem*n);
        }
        else
        {
            int n = movState->audio.frame_size;
            int len = -movState->audio.samples_pos;
            if(rem > len) rem = len;

            /* Add samples by copying the first sample */
            if(n == 1)
            {
                uint8_t sample = ((uint8_t*)movState->audio.samples)[0];
                uint8_t *q = (uint8_t*)samples + audio_size;
                for(int i = 0;i < rem;i++)
                    *(q++) = sample;
            }
            else if(n == 2)
            {
                uint16_t sample = ((uint16_t*)movState->audio.samples)[0];
                uint16_t *q = (uint16_t*)samples + audio_size;
                for(int i = 0;i < rem;i++)
                    *(q++) = sample;
            }
            else if(n == 4)
            {
                uint32_t sample = ((uint32_t*)movState->audio.samples)[0];
                uint32_t *q = (uint32_t*)samples + audio_size;
                for(int i = 0;i < rem;i++)
                    *(q++) = sample;
            }
            else if(n == 8)
            {
                uint64_t sample = ((uint64_t*)movState->audio.samples)[0];
                uint64_t *q = (uint64_t*)samples + audio_size;
                for(int i = 0;i < rem;i++)
                    *(q++) = sample;
            }
            else
            {
                uint8_t *sample = movState->audio.samples;
                uint8_t *q = samples + audio_size*n;
                for(int i = 0;i < rem;i++)
                {
                    memcpy(q, sample, n);
                    q += n;
                }
            }
        }

        movState->audio.samples_pos += rem;
        movState->audio.current_pts += (double)rem / movState->audio.st->codec->sample_rate;
        audio_size += rem;
    }

    return audio_size * movState->audio.frame_size;
}

static int audio_thread(void *userdata)
{
    MovieState *movState = (MovieState*)userdata;
    uint8_t *samples = NULL;
    ALsizei buffer_len;
    ALenum fmt;

    alGenBuffers(AUDIO_BUFFER_QUEUE_SIZE, movState->audio.buffer);
    alGenSources(1, &movState->audio.source);

    alSourcei(movState->audio.source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSourcei(movState->audio.source, AL_ROLLOFF_FACTOR, 0);

    av_new_packet(&movState->audio.pkt, 0);

    /* Find a suitable format for OpenAL. */
    movState->audio.format = AL_NONE;
    if(movState->audio.st->codec->sample_fmt == AV_SAMPLE_FMT_U8 ||
       movState->audio.st->codec->sample_fmt == AV_SAMPLE_FMT_U8P)
    {
        movState->audio.dst_sample_fmt = AV_SAMPLE_FMT_U8;
        movState->audio.frame_size = 1;
        if(movState->audio.st->codec->channel_layout == AV_CH_LAYOUT_7POINT1 &&
           alIsExtensionPresent("AL_EXT_MCFORMATS") &&
           (fmt=alGetEnumValue("AL_FORMAT_71CHN8")) != AL_NONE && fmt != -1)
        {
            movState->audio.dst_ch_layout = movState->audio.st->codec->channel_layout;
            movState->audio.frame_size *= 8;
            movState->audio.format = fmt;
        }
        if((movState->audio.st->codec->channel_layout == AV_CH_LAYOUT_5POINT1 ||
            movState->audio.st->codec->channel_layout == AV_CH_LAYOUT_5POINT1_BACK) &&
           alIsExtensionPresent("AL_EXT_MCFORMATS") &&
           (fmt=alGetEnumValue("AL_FORMAT_51CHN8")) != AL_NONE && fmt != -1)
        {
            movState->audio.dst_ch_layout = movState->audio.st->codec->channel_layout;
            movState->audio.frame_size *= 6;
            movState->audio.format = fmt;
        }
        if(movState->audio.st->codec->channel_layout == AV_CH_LAYOUT_MONO)
        {
            movState->audio.dst_ch_layout = AV_CH_LAYOUT_MONO;
            movState->audio.frame_size *= 1;
            movState->audio.format = AL_FORMAT_MONO8;
        }
        if(movState->audio.format == AL_NONE)
        {
            movState->audio.dst_ch_layout = AV_CH_LAYOUT_STEREO;
            movState->audio.frame_size *= 2;
            movState->audio.format = AL_FORMAT_STEREO8;
        }
    }
    if((movState->audio.st->codec->sample_fmt == AV_SAMPLE_FMT_FLT ||
        movState->audio.st->codec->sample_fmt == AV_SAMPLE_FMT_FLTP) &&
       alIsExtensionPresent("AL_EXT_FLOAT32"))
    {
        movState->audio.dst_sample_fmt = AV_SAMPLE_FMT_FLT;
        movState->audio.frame_size = 4;
        if(movState->audio.st->codec->channel_layout == AV_CH_LAYOUT_7POINT1 &&
           alIsExtensionPresent("AL_EXT_MCFORMATS") &&
           (fmt=alGetEnumValue("AL_FORMAT_71CHN32")) != AL_NONE && fmt != -1)
        {
            movState->audio.dst_ch_layout = movState->audio.st->codec->channel_layout;
            movState->audio.frame_size *= 8;
            movState->audio.format = fmt;
        }
        if((movState->audio.st->codec->channel_layout == AV_CH_LAYOUT_5POINT1 ||
            movState->audio.st->codec->channel_layout == AV_CH_LAYOUT_5POINT1_BACK) &&
           alIsExtensionPresent("AL_EXT_MCFORMATS") &&
           (fmt=alGetEnumValue("AL_FORMAT_51CHN32")) != AL_NONE && fmt != -1)
        {
            movState->audio.dst_ch_layout = movState->audio.st->codec->channel_layout;
            movState->audio.frame_size *= 6;
            movState->audio.format = fmt;
        }
        if(movState->audio.st->codec->channel_layout == AV_CH_LAYOUT_MONO)
        {
            movState->audio.dst_ch_layout = AV_CH_LAYOUT_MONO;
            movState->audio.frame_size *= 1;
            movState->audio.format = AL_FORMAT_MONO_FLOAT32;
        }
        if(movState->audio.format == AL_NONE)
        {
            movState->audio.dst_ch_layout = AV_CH_LAYOUT_STEREO;
            movState->audio.frame_size *= 2;
            movState->audio.format = AL_FORMAT_STEREO_FLOAT32;
        }
    }
    if(movState->audio.format == AL_NONE)
    {
        movState->audio.dst_sample_fmt = AV_SAMPLE_FMT_S16;
        movState->audio.frame_size = 2;
        if(movState->audio.st->codec->channel_layout == AV_CH_LAYOUT_7POINT1 &&
           alIsExtensionPresent("AL_EXT_MCFORMATS") &&
           (fmt=alGetEnumValue("AL_FORMAT_71CHN16")) != AL_NONE && fmt != -1)
        {
            movState->audio.dst_ch_layout = movState->audio.st->codec->channel_layout;
            movState->audio.frame_size *= 8;
            movState->audio.format = fmt;
        }
        if((movState->audio.st->codec->channel_layout == AV_CH_LAYOUT_5POINT1 ||
            movState->audio.st->codec->channel_layout == AV_CH_LAYOUT_5POINT1_BACK) &&
           alIsExtensionPresent("AL_EXT_MCFORMATS") &&
           (fmt=alGetEnumValue("AL_FORMAT_51CHN16")) != AL_NONE && fmt != -1)
        {
            movState->audio.dst_ch_layout = movState->audio.st->codec->channel_layout;
            movState->audio.frame_size *= 6;
            movState->audio.format = fmt;
        }
        if(movState->audio.st->codec->channel_layout == AV_CH_LAYOUT_MONO)
        {
            movState->audio.dst_ch_layout = AV_CH_LAYOUT_MONO;
            movState->audio.frame_size *= 1;
            movState->audio.format = AL_FORMAT_MONO16;
        }
        if(movState->audio.format == AL_NONE)
        {
            movState->audio.dst_ch_layout = AV_CH_LAYOUT_STEREO;
            movState->audio.frame_size *= 2;
            movState->audio.format = AL_FORMAT_STEREO16;
        }
    }
    buffer_len = AUDIO_BUFFER_TIME * movState->audio.st->codec->sample_rate / 1000 *
                 movState->audio.frame_size;
    samples = av_malloc(buffer_len);

    movState->audio.samples = NULL;
    movState->audio.samples_max = 0;
    movState->audio.samples_pos = 0;
    movState->audio.samples_len = 0;

    if(!(movState->audio.decoded_aframe=av_frame_alloc()))
    {
        fprintf(stderr, "Failed to allocate audio frame\n");
        goto finish;
    }

    movState->audio.swres_ctx = swr_alloc_set_opts(NULL,
        movState->audio.dst_ch_layout,
        movState->audio.dst_sample_fmt,
        movState->audio.st->codec->sample_rate,
        movState->audio.st->codec->channel_layout ?
            movState->audio.st->codec->channel_layout :
            (uint64_t)av_get_default_channel_layout(movState->audio.st->codec->channels),
        movState->audio.st->codec->sample_fmt,
        movState->audio.st->codec->sample_rate,
        0, NULL
    );
    if(!movState->audio.swres_ctx || swr_init(movState->audio.swres_ctx) != 0)
    {
        fprintf(stderr, "Failed to initialize audio converter\n");
        goto finish;
    }

    almtx_lock(&movState->audio.src_mutex);
    while(alGetError() == AL_NO_ERROR && !movState->quit)
    {
        /* First remove any processed buffers. */
        ALint processed;
        alGetSourcei(movState->audio.source, AL_BUFFERS_PROCESSED, &processed);
        alSourceUnqueueBuffers(movState->audio.source, processed, (ALuint[AUDIO_BUFFER_QUEUE_SIZE]){});

        /* Refill the buffer queue. */
        ALint queued;
        alGetSourcei(movState->audio.source, AL_BUFFERS_QUEUED, &queued);
        while(queued < AUDIO_BUFFER_QUEUE_SIZE)
        {
            int audio_size;

            /* Read the next chunk of data, fill the buffer, and queue it on
             * the source */
            audio_size = read_audio(movState, samples, buffer_len);
            if(audio_size < 0) break;

            ALuint bufid = movState->audio.buffer[movState->audio.buffer_idx++];
            movState->audio.buffer_idx %= AUDIO_BUFFER_QUEUE_SIZE;

            alBufferData(bufid, movState->audio.format, samples, audio_size,
                         movState->audio.st->codec->sample_rate);
            alSourceQueueBuffers(movState->audio.source, 1, &bufid);
            queued++;
        }

        /* Check that the source is playing. */
        ALint state;
        alGetSourcei(movState->audio.source, AL_SOURCE_STATE, &state);
        if(state == AL_STOPPED)
        {
            /* AL_STOPPED means there was an underrun. Double-check that all
             * processed buffers are removed, then rewind the source to get it
             * back into an AL_INITIAL state. */
            alGetSourcei(movState->audio.source, AL_BUFFERS_PROCESSED, &processed);
            alSourceUnqueueBuffers(movState->audio.source, processed, (ALuint[AUDIO_BUFFER_QUEUE_SIZE]){});
            alSourceRewind(movState->audio.source);
            continue;
        }

        almtx_unlock(&movState->audio.src_mutex);

        /* (re)start the source if needed, and wait for a buffer to finish */
        if(state != AL_PLAYING && state != AL_PAUSED)
        {
            alGetSourcei(movState->audio.source, AL_BUFFERS_QUEUED, &queued);
            if(queued > 0) alSourcePlay(movState->audio.source);
        }
        SDL_Delay(AUDIO_BUFFER_TIME);

        almtx_lock(&movState->audio.src_mutex);
    }
    almtx_unlock(&movState->audio.src_mutex);

finish:
    av_frame_free(&movState->audio.decoded_aframe);
    swr_free(&movState->audio.swres_ctx);

    av_freep(&samples);
    av_freep(&movState->audio.samples);

    alDeleteSources(1, &movState->audio.source);
    alDeleteBuffers(AUDIO_BUFFER_QUEUE_SIZE, movState->audio.buffer);

    return 0;
}


static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque)
{
    (void)interval;

    SDL_PushEvent(&(SDL_Event){ .user={.type=FF_REFRESH_EVENT, .data1=opaque} });
    return 0; /* 0 means stop timer */
}

/* Schedule a video refresh in 'delay' ms */
static void schedule_refresh(MovieState *movState, int delay)
{
    SDL_AddTimer(delay, sdl_refresh_timer_cb, movState);
}

static void video_display(MovieState *movState, SDL_Window *screen, SDL_Renderer *renderer)
{
    VideoPicture *vp = &movState->video.pictq[movState->video.pictq_rindex];

    if(!vp->bmp)
        return;

    float aspect_ratio;
    int win_w, win_h;
    int w, h, x, y;

    if(movState->video.st->codec->sample_aspect_ratio.num == 0)
        aspect_ratio = 0.0f;
    else
    {
        aspect_ratio = av_q2d(movState->video.st->codec->sample_aspect_ratio) *
                       movState->video.st->codec->width /
                       movState->video.st->codec->height;
    }
    if(aspect_ratio <= 0.0f)
    {
        aspect_ratio = (float)movState->video.st->codec->width /
                       (float)movState->video.st->codec->height;
    }

    SDL_GetWindowSize(screen, &win_w, &win_h);
    h = win_h;
    w = ((int)rint(h * aspect_ratio) + 3) & ~3;
    if(w > win_w)
    {
        w = win_w;
        h = ((int)rint(w / aspect_ratio) + 3) & ~3;
    }
    x = (win_w - w) / 2;
    y = (win_h - h) / 2;

    SDL_RenderCopy(renderer, vp->bmp,
        &(SDL_Rect){ .x=0, .y=0, .w=vp->width, .h=vp->height },
        &(SDL_Rect){ .x=x, .y=y, .w=w, .h=h }
    );
    SDL_RenderPresent(renderer);
}

static void video_refresh_timer(MovieState *movState, SDL_Window *screen, SDL_Renderer *renderer)
{
    if(!movState->video.st)
    {
        schedule_refresh(movState, 100);
        return;
    }

    almtx_lock(&movState->video.pictq_mutex);
retry:
    if(movState->video.pictq_size == 0)
        schedule_refresh(movState, 1);
    else
    {
        VideoPicture *vp = &movState->video.pictq[movState->video.pictq_rindex];
        double actual_delay, delay, sync_threshold, ref_clock, diff;

        movState->video.current_pts = vp->pts;
        movState->video.current_pts_time = av_gettime();

        delay = vp->pts - movState->video.frame_last_pts; /* the pts from last time */
        if(delay <= 0 || delay >= 1.0)
        {
            /* if incorrect delay, use previous one */
            delay = movState->video.frame_last_delay;
        }
        /* save for next time */
        movState->video.frame_last_delay = delay;
        movState->video.frame_last_pts = vp->pts;

        /* Update delay to sync to clock if not master source. */
        if(movState->av_sync_type != AV_SYNC_VIDEO_MASTER)
        {
            ref_clock = get_master_clock(movState);
            diff = vp->pts - ref_clock;

            /* Skip or repeat the frame. Take delay into account. */
            sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
            if(fabs(diff) < AV_NOSYNC_THRESHOLD)
            {
                if(diff <= -sync_threshold)
                    delay = 0;
                else if(diff >= sync_threshold)
                    delay = 2 * delay;
            }
        }

        movState->video.frame_timer += delay;
        /* Compute the REAL delay. */
        actual_delay = movState->video.frame_timer - (av_gettime() / 1000000.0);
        if(!(actual_delay >= 0.010))
        {
            /* We don't have time to handle this picture, just skip to the next one. */
            movState->video.pictq_rindex = (movState->video.pictq_rindex+1)%VIDEO_PICTURE_QUEUE_SIZE;
            movState->video.pictq_size--;
            alcnd_signal(&movState->video.pictq_cond);
            goto retry;
        }
        schedule_refresh(movState, (int)(actual_delay*1000.0 + 0.5));

        /* Show the picture! */
        video_display(movState, screen, renderer);

        /* Update queue for next picture. */
        movState->video.pictq_rindex = (movState->video.pictq_rindex+1)%VIDEO_PICTURE_QUEUE_SIZE;
        movState->video.pictq_size--;
        alcnd_signal(&movState->video.pictq_cond);
    }
    almtx_unlock(&movState->video.pictq_mutex);
}


static void update_picture(MovieState *movState, bool *first_update, SDL_Window *screen, SDL_Renderer *renderer)
{
    VideoPicture *vp = &movState->video.pictq[movState->video.pictq_windex];

    /* allocate or resize the buffer! */
    if(!vp->bmp || vp->width != movState->video.st->codec->width ||
                   vp->height != movState->video.st->codec->height)
    {
        if(vp->bmp)
            SDL_DestroyTexture(vp->bmp);
        vp->bmp = SDL_CreateTexture(
            renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
            movState->video.st->codec->coded_width, movState->video.st->codec->coded_height
        );
        if(!vp->bmp)
            fprintf(stderr, "Failed to create YV12 texture!\n");
        vp->width = movState->video.st->codec->width;
        vp->height = movState->video.st->codec->height;

        if(*first_update && vp->width > 0 && vp->height > 0)
        {
            /* For the first update, set the window size to the video size. */
            *first_update = false;

            int w = vp->width;
            int h = vp->height;
            if(movState->video.st->codec->sample_aspect_ratio.num != 0 &&
               movState->video.st->codec->sample_aspect_ratio.den != 0)
            {
                double aspect_ratio = av_q2d(movState->video.st->codec->sample_aspect_ratio);
                if(aspect_ratio >= 1.0)
                    w = (int)(w*aspect_ratio + 0.5);
                else if(aspect_ratio > 0.0)
                    h = (int)(h/aspect_ratio + 0.5);
            }
            SDL_SetWindowSize(screen, w, h);
        }
    }

    if(vp->bmp)
    {
        AVFrame *frame = movState->video.decoded_vframe;
        void *pixels = NULL;
        int pitch = 0;

        if(movState->video.st->codec->pix_fmt == PIX_FMT_YUV420P)
            SDL_UpdateYUVTexture(vp->bmp, NULL,
                frame->data[0], frame->linesize[0],
                frame->data[1], frame->linesize[1],
                frame->data[2], frame->linesize[2]
            );
        else if(SDL_LockTexture(vp->bmp, NULL, &pixels, &pitch) != 0)
            fprintf(stderr, "Failed to lock texture\n");
        else
        {
            // Convert the image into YUV format that SDL uses
            int coded_w = movState->video.st->codec->coded_width;
            int coded_h = movState->video.st->codec->coded_height;
            int w = movState->video.st->codec->width;
            int h = movState->video.st->codec->height;
            if(!movState->video.swscale_ctx)
                movState->video.swscale_ctx = sws_getContext(
                    w, h, movState->video.st->codec->pix_fmt,
                    w, h, PIX_FMT_YUV420P, SWS_X, NULL, NULL, NULL
                );

            /* point pict at the queue */
            AVPicture pict;
            pict.data[0] = pixels;
            pict.data[2] = pict.data[0] + coded_w*coded_h;
            pict.data[1] = pict.data[2] + coded_w*coded_h/4;

            pict.linesize[0] = pitch;
            pict.linesize[2] = pitch / 2;
            pict.linesize[1] = pitch / 2;

            sws_scale(movState->video.swscale_ctx, (const uint8_t**)frame->data,
                      frame->linesize, 0, h, pict.data, pict.linesize);
            SDL_UnlockTexture(vp->bmp);
        }
    }

    almtx_lock(&movState->video.pictq_mutex);
    vp->updated = true;
    almtx_unlock(&movState->video.pictq_mutex);
    alcnd_signal(&movState->video.pictq_cond);
}

static int queue_picture(MovieState *movState, double pts)
{
    /* Wait until we have space for a new pic */
    almtx_lock(&movState->video.pictq_mutex);
    while(movState->video.pictq_size >= VIDEO_PICTURE_QUEUE_SIZE && !movState->quit)
        alcnd_wait(&movState->video.pictq_cond, &movState->video.pictq_mutex);
    almtx_unlock(&movState->video.pictq_mutex);

    if(movState->quit)
        return -1;

    VideoPicture *vp = &movState->video.pictq[movState->video.pictq_windex];

    /* We have to create/update the picture in the main thread  */
    vp->updated = false;
    SDL_PushEvent(&(SDL_Event){ .user={.type=FF_UPDATE_EVENT, .data1=movState} });

    /* Wait until the picture is updated. */
    almtx_lock(&movState->video.pictq_mutex);
    while(!vp->updated && !movState->quit)
        alcnd_wait(&movState->video.pictq_cond, &movState->video.pictq_mutex);
    almtx_unlock(&movState->video.pictq_mutex);
    if(movState->quit)
        return -1;
    vp->pts = pts;

    movState->video.pictq_windex = (movState->video.pictq_windex+1)%VIDEO_PICTURE_QUEUE_SIZE;
    almtx_lock(&movState->video.pictq_mutex);
    movState->video.pictq_size++;
    almtx_unlock(&movState->video.pictq_mutex);

    return 0;
}

static double synchronize_video(MovieState *movState, double pts)
{
    double frame_delay;

    if(pts == 0.0) /* if we aren't given a pts, set it to the clock */
        pts = movState->video.clock;
    else /* if we have pts, set video clock to it */
        movState->video.clock = pts;

    /* update the video clock */
    frame_delay = av_q2d(movState->video.st->codec->time_base);
    /* if we are repeating a frame, adjust clock accordingly */
    frame_delay += movState->video.decoded_vframe->repeat_pict * (frame_delay * 0.5);
    movState->video.clock += frame_delay;
    return pts;
}

int video_thread(void *arg)
{
    MovieState *movState = (MovieState*)arg;
    AVPacket *packet = (AVPacket[1]){};
    int64_t saved_pts, pkt_pts;
    int frameFinished;

    movState->video.decoded_vframe = av_frame_alloc();
    while(packet_queue_get(&movState->video.q, packet, movState) >= 0)
    {
        if(packet->data == flush_pkt.data)
        {
            avcodec_flush_buffers(movState->video.st->codec);

            almtx_lock(&movState->video.pictq_mutex);
            movState->video.pictq_size = 0;
            movState->video.pictq_rindex = 0;
            movState->video.pictq_windex = 0;
            almtx_unlock(&movState->video.pictq_mutex);

            movState->video.clock = av_q2d(movState->video.st->time_base)*packet->pts;
            movState->video.current_pts = movState->video.clock;
            movState->video.current_pts_time = av_gettime();
            continue;
        }

        pkt_pts = packet->pts;

        /* Decode video frame */
        avcodec_decode_video2(movState->video.st->codec, movState->video.decoded_vframe,
                              &frameFinished, packet);
        if(pkt_pts != AV_NOPTS_VALUE && !movState->video.decoded_vframe->opaque)
        {
            /* Store the packet's original pts in the frame, in case the frame
             * is not finished decoding yet. */
            saved_pts = pkt_pts;
            movState->video.decoded_vframe->opaque = &saved_pts;
        }

        av_free_packet(packet);

        if(frameFinished)
        {
            double pts = av_q2d(movState->video.st->time_base);
            if(packet->dts != AV_NOPTS_VALUE)
                pts *= packet->dts;
            else if(movState->video.decoded_vframe->opaque)
                pts *= *(int64_t*)movState->video.decoded_vframe->opaque;
            else
                pts *= 0.0;
            movState->video.decoded_vframe->opaque = NULL;

            pts = synchronize_video(movState, pts);
            if(queue_picture(movState, pts) < 0)
                break;
        }
    }

    sws_freeContext(movState->video.swscale_ctx);
    movState->video.swscale_ctx = NULL;
    av_frame_free(&movState->video.decoded_vframe);
    return 0;
}


static int stream_component_open(MovieState *movState, int stream_index)
{
    AVFormatContext *pFormatCtx = movState->pFormatCtx;
    AVCodecContext *codecCtx;
    AVCodec *codec;

    if(stream_index < 0 || (unsigned int)stream_index >= pFormatCtx->nb_streams)
        return -1;

    /* Get a pointer to the codec context for the video stream, and open the
     * associated codec */
    codecCtx = pFormatCtx->streams[stream_index]->codec;

    codec = avcodec_find_decoder(codecCtx->codec_id);
    if(!codec || avcodec_open2(codecCtx, codec, NULL) < 0)
    {
        fprintf(stderr, "Unsupported codec!\n");
        return -1;
    }

    /* Initialize and start the media type handler */
    switch(codecCtx->codec_type)
    {
        case AVMEDIA_TYPE_AUDIO:
            movState->audioStream = stream_index;
            movState->audio.st = pFormatCtx->streams[stream_index];

            /* Averaging filter for audio sync */
            movState->audio.diff_avg_coef = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
            /* Correct audio only if larger error than this */
            movState->audio.diff_threshold = 2.0 * 0.050/* 50 ms */;

            memset(&movState->audio.pkt, 0, sizeof(movState->audio.pkt));
            if(althrd_create(&movState->audio.thread, audio_thread, movState) != althrd_success)
            {
                movState->audioStream = -1;
                movState->audio.st = NULL;
            }
            break;

        case AVMEDIA_TYPE_VIDEO:
            movState->videoStream = stream_index;
            movState->video.st = pFormatCtx->streams[stream_index];

            movState->video.current_pts_time = av_gettime();
            movState->video.frame_timer = (double)movState->video.current_pts_time /
                                          1000000.0;
            movState->video.frame_last_delay = 40e-3;

            if(althrd_create(&movState->video.thread, video_thread, movState) != althrd_success)
            {
                movState->videoStream = -1;
                movState->video.st = NULL;
            }
            break;

        default:
            break;
    }

    return 0;
}

static int decode_interrupt_cb(void *ctx)
{
    return ((MovieState*)ctx)->quit;
}

int decode_thread(void *arg)
{
    MovieState *movState = (MovieState *)arg;
    AVFormatContext *fmtCtx = movState->pFormatCtx;
    AVPacket *packet = (AVPacket[1]){};
    int video_index = -1;
    int audio_index = -1;

    movState->videoStream = -1;
    movState->audioStream = -1;

    /* Dump information about file onto standard error */
    av_dump_format(fmtCtx, 0, movState->filename, 0);

    /* Find the first video and audio streams */
    for(unsigned int i = 0;i < fmtCtx->nb_streams;i++)
    {
        if(fmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && video_index < 0)
            video_index = i;
        else if(fmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && audio_index < 0)
            audio_index = i;
    }
    movState->external_clock_base = av_gettime();
    if(audio_index >= 0)
        stream_component_open(movState, audio_index);
    if(video_index >= 0)
        stream_component_open(movState, video_index);

    if(movState->videoStream < 0 && movState->audioStream < 0)
    {
        fprintf(stderr, "%s: could not open codecs\n", movState->filename);
        goto fail;
    }

    /* Main packet handling loop */
    while(!movState->quit)
    {
        if(movState->seek_req)
        {
            int64_t seek_target = movState->seek_pos;
            int stream_index= -1;

            /* Prefer seeking on the video stream. */
            if(movState->videoStream >= 0)
                stream_index = movState->videoStream;
            else if(movState->audioStream >= 0)
                stream_index = movState->audioStream;

            /* Get a seek timestamp for the appropriate stream. */
            int64_t timestamp = seek_target;
            if(stream_index >= 0)
                timestamp = av_rescale_q(seek_target, AV_TIME_BASE_Q, fmtCtx->streams[stream_index]->time_base);

            if(av_seek_frame(movState->pFormatCtx, stream_index, timestamp, 0) < 0)
                fprintf(stderr, "%s: error while seeking\n", movState->pFormatCtx->filename);
            else
            {
                /* Seek successful, clear the packet queues and send a special
                 * 'flush' packet with the new stream clock time. */
                if(movState->audioStream >= 0)
                {
                    packet_queue_clear(&movState->audio.q);
                    flush_pkt.pts = av_rescale_q(seek_target, AV_TIME_BASE_Q,
                        fmtCtx->streams[movState->audioStream]->time_base
                    );
                    packet_queue_put(&movState->audio.q, &flush_pkt);
                }
                if(movState->videoStream >= 0)
                {
                    packet_queue_clear(&movState->video.q);
                    flush_pkt.pts = av_rescale_q(seek_target, AV_TIME_BASE_Q,
                        fmtCtx->streams[movState->videoStream]->time_base
                    );
                    packet_queue_put(&movState->video.q, &flush_pkt);
                }
                movState->external_clock_base = av_gettime() - seek_target;
            }
            movState->seek_req = false;
        }

        if(movState->audio.q.size >= MAX_AUDIOQ_SIZE ||
           movState->video.q.size >= MAX_VIDEOQ_SIZE)
        {
            SDL_Delay(10);
            continue;
        }

        if(av_read_frame(movState->pFormatCtx, packet) < 0)
        {
            packet_queue_flush(&movState->video.q);
            packet_queue_flush(&movState->audio.q);
            break;
        }

        /* Place the packet in the queue it's meant for, or discard it. */
        if(packet->stream_index == movState->videoStream)
            packet_queue_put(&movState->video.q, packet);
        else if(packet->stream_index == movState->audioStream)
            packet_queue_put(&movState->audio.q, packet);
        else
            av_free_packet(packet);
    }

    /* all done - wait for it */
    while(!movState->quit)
    {
        if(movState->audio.q.nb_packets == 0 && movState->video.q.nb_packets == 0)
            break;
        SDL_Delay(100);
    }

fail:
    movState->quit = true;
    packet_queue_flush(&movState->video.q);
    packet_queue_flush(&movState->audio.q);

    if(movState->videoStream >= 0)
        althrd_join(movState->video.thread, NULL);
    if(movState->audioStream >= 0)
        althrd_join(movState->audio.thread, NULL);

    SDL_PushEvent(&(SDL_Event){ .user={.type=FF_QUIT_EVENT, .data1=movState} });

    return 0;
}


static void stream_seek(MovieState *movState, double incr)
{
    if(!movState->seek_req)
    {
        double newtime = get_master_clock(movState)+incr;
        if(newtime <= 0.0) movState->seek_pos = 0;
        else movState->seek_pos = (int64_t)(newtime * AV_TIME_BASE);
        movState->seek_req = true;
    }
}

int main(int argc, char *argv[])
{
    SDL_Event event;
    MovieState *movState;
    bool first_update = true;
    SDL_Window   *screen;
    SDL_Renderer *renderer;
    ALCdevice  *device;
    ALCcontext *context;

    if(argc < 2)
    {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }
    /* Register all formats and codecs */
    av_register_all();
    /* Initialize networking protocols */
    avformat_network_init();

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER))
    {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        return 1;
    }

    /* Make a window to put our video */
    screen = SDL_CreateWindow("alffplay", 0, 0, 640, 480, SDL_WINDOW_RESIZABLE);
    if(!screen)
    {
        fprintf(stderr, "SDL: could not set video mode - exiting\n");
        return 1;
    }
    /* Make a renderer to handle the texture image surface and rendering. */
    renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED);
    if(renderer)
    {
        SDL_RendererInfo rinf;
        bool ok = false;

        /* Make sure the renderer supports YV12 textures. If not, fallback to a
         * software renderer. */
        if(SDL_GetRendererInfo(renderer, &rinf) == 0)
        {
            for(Uint32 i = 0;!ok && i < rinf.num_texture_formats;i++)
                ok = (rinf.texture_formats[i] == SDL_PIXELFORMAT_YV12);
        }
        if(!ok)
        {
            fprintf(stderr, "YV12 pixelformat textures not supported on renderer %s\n", rinf.name);
            SDL_DestroyRenderer(renderer);
            renderer = NULL;
        }
    }
    if(!renderer)
        renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_SOFTWARE);
    if(!renderer)
    {
        fprintf(stderr, "SDL: could not create renderer - exiting\n");
        return 1;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, NULL);
    SDL_RenderPresent(renderer);

    /* Open an audio device */
    device = alcOpenDevice(NULL);
    if(!device)
    {
        fprintf(stderr, "OpenAL: could not open device - exiting\n");
        return 1;
    }
    context = alcCreateContext(device, NULL);
    if(!context)
    {
        fprintf(stderr, "OpenAL: could not create context - exiting\n");
        return 1;
    }
    if(alcMakeContextCurrent(context) == ALC_FALSE)
    {
        fprintf(stderr, "OpenAL: could not make context current - exiting\n");
        return 1;
    }

    if(!alIsExtensionPresent("AL_SOFT_source_length"))
    {
        fprintf(stderr, "Required AL_SOFT_source_length not supported - exiting\n");
        return 1;
    }

    if(!alIsExtensionPresent("AL_SOFT_source_latency"))
        fprintf(stderr, "AL_SOFT_source_latency not supported, audio may be a bit laggy.\n");
    else
    {
        alGetSourcedvSOFT = alGetProcAddress("alGetSourcedvSOFT");
        has_latency_check = true;
    }


    movState = av_mallocz(sizeof(MovieState));

    av_strlcpy(movState->filename, argv[1], sizeof(movState->filename));

    packet_queue_init(&movState->audio.q);
    packet_queue_init(&movState->video.q);

    almtx_init(&movState->video.pictq_mutex, almtx_plain);
    alcnd_init(&movState->video.pictq_cond);
    almtx_init(&movState->audio.src_mutex, almtx_recursive);

    movState->av_sync_type = DEFAULT_AV_SYNC_TYPE;

    movState->pFormatCtx = avformat_alloc_context();
    movState->pFormatCtx->interrupt_callback = (AVIOInterruptCB){.callback=decode_interrupt_cb, .opaque=movState};

    if(avio_open2(&movState->pFormatCtx->pb, movState->filename, AVIO_FLAG_READ,
                  &movState->pFormatCtx->interrupt_callback, NULL))
    {
        fprintf(stderr, "Failed to open %s\n", movState->filename);
        return 1;
    }

    /* Open movie file */
    if(avformat_open_input(&movState->pFormatCtx, movState->filename, NULL, NULL) != 0)
    {
        fprintf(stderr, "Failed to open %s\n", movState->filename);
        return 1;
    }

    /* Retrieve stream information */
    if(avformat_find_stream_info(movState->pFormatCtx, NULL) < 0)
    {
        fprintf(stderr, "%s: failed to find stream info\n", movState->filename);
        return 1;
    }

    schedule_refresh(movState, 40);


    if(althrd_create(&movState->parse_thread, decode_thread, movState) != althrd_success)
    {
        fprintf(stderr, "Failed to create parse thread!\n");
        return 1;
    }
    while(SDL_WaitEvent(&event) == 1)
    {
        switch(event.type)
        {
            case SDL_KEYDOWN:
                switch(event.key.keysym.sym)
                {
                    case SDLK_ESCAPE:
                        movState->quit = true;
                        break;

                    case SDLK_LEFT:
                        stream_seek(movState, -10.0);
                        break;
                    case SDLK_RIGHT:
                        stream_seek(movState, 10.0);
                        break;
                    case SDLK_UP:
                        stream_seek(movState, 30.0);
                        break;
                    case SDLK_DOWN:
                        stream_seek(movState, -30.0);
                        break;

                    default:
                        break;
                }
                break;

            case SDL_WINDOWEVENT:
                switch(event.window.event)
                {
                    case SDL_WINDOWEVENT_RESIZED:
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                        SDL_RenderFillRect(renderer, NULL);
                        break;

                    default:
                        break;
                }
                break;

            case SDL_QUIT:
                movState->quit = true;
                break;

            case FF_UPDATE_EVENT:
                update_picture(event.user.data1, &first_update, screen, renderer);
                break;

            case FF_REFRESH_EVENT:
                video_refresh_timer(event.user.data1, screen, renderer);
                break;

            case FF_QUIT_EVENT:
                althrd_join(movState->parse_thread, NULL);

                avformat_close_input(&movState->pFormatCtx);

                almtx_destroy(&movState->audio.src_mutex);
                almtx_destroy(&movState->video.pictq_mutex);
                alcnd_destroy(&movState->video.pictq_cond);
                packet_queue_deinit(&movState->video.q);
                packet_queue_deinit(&movState->audio.q);

                alcMakeContextCurrent(NULL);
                alcDestroyContext(context);
                alcCloseDevice(device);

                SDL_Quit();
                exit(0);

            default:
                break;
        }
    }

    fprintf(stderr, "SDL_WaitEvent error - %s\n", SDL_GetError());
    return 1;
}
