/* GoTube FLV playback pipeline.
 * FFmpeg baseline: GT12-MEDIA-0001 (Lavc51.40.4/Lavf51.12.1). */
#include "gotube.h"
#include <pspaudio.h>
#include <malloc.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avio.h>

#define Y_STRIDE_MAX 768
#define Y_HEIGHT_MAX 512
#define UV_STRIDE_MAX (Y_STRIDE_MAX / 2)
#define UV_HEIGHT_MAX (Y_HEIGHT_MAX / 2)

/* FUN_00023ef8 consumes the decoder's three 8-bit Y/V/U planes directly as
 * GU_PSM_T8 textures.  Keep the same plane contract instead of manufacturing
 * a 32-bit RGB texture (which is not the native player path). */
static const unsigned char *frame_y_ptr;
static const unsigned char *frame_v_ptr;
static const unsigned char *frame_u_ptr;
static volatile int frame_index = -1;
static volatile int frame_width = 0;
static volatile int frame_height = 0;
static volatile int frame_y_stride = 0;
static volatile int frame_uv_stride = 0;
static const unsigned char *frame_rgba_ptr;
static volatile int frame_rgba_stride = 0;
static volatile int player_status = 0;
static volatile int player_progress_value = 0;
static volatile int player_cancel = 0;
static volatile int player_pause = 0;
static SceUID player_thread = -1;
static char player_url[2048];
static char player_audio_url[2048];
static char player_source_url[512];
static int player_local_file = 0;
static int player_frames_decoded = 0;
static volatile int player_time_value = 0;
static volatile int player_duration_value = 0;
/* Shared dispatcher VA 0x2097c: Triangle cycles five overlay states and
 * Select increments the renderer's 0..14 vertical-position mode. */
static int player_overlay_mode = 3;
static int player_render_mode = 0;
static int player_speed_mode = 10;

/* The original player does not decode in its demux thread.  FUN_0002220c
 * queues packets and the workers at 0x231c4 (video) and 0x25424 (audio)
 * consume them independently. */
typedef struct PlayerPacketNode {
    AVPacket packet;
    struct PlayerPacketNode *next;
} PlayerPacketNode;

typedef struct {
    PlayerPacketNode *head, *tail;
    volatile int bytes, ended, abort;
    SceUID lock;
} PlayerPacketQueue;

typedef struct {
    PlayerPacketQueue video_q, audio_q;
    AVCodecContext *video, *audio;
    AVFrame *picture;
    AVRational video_time_base;
    int audio_reserved, audio_frame_samples;
    SceUID codec_lock;
    volatile int failed;
    int hardware_avc;
} PlayerPipeline;

static void packet_queue_init(PlayerPacketQueue *q, const char *name)
{
    memset(q, 0, sizeof(*q));
    q->lock = sceKernelCreateSema(name, 0, 1, 1, NULL);
}

static void packet_queue_finish(PlayerPacketQueue *q) { q->ended = 1; }
static void packet_queue_abort(PlayerPacketQueue *q) { q->abort = 1; }

static int packet_queue_put(PlayerPacketQueue *q, AVPacket *packet)
{
    PlayerPacketNode *node;
    /* Original prebuffer loop limits each stream to 0x80000 before dispatch. */
    while (!player_cancel && !q->abort && q->bytes + packet->size > 0x80000)
        sceKernelDelayThreadCB(1000);
    if (player_cancel || q->abort) return -1;
    node = malloc(sizeof(*node));
    if (!node) return -1;
    node->packet = *packet;
    node->next = NULL;
    if (sceKernelWaitSema(q->lock, 1, NULL) < 0) {
        free(node);
        return -1;
    }
    if (q->tail) q->tail->next = node; else q->head = node;
    q->tail = node;
    q->bytes += packet->size;
    sceKernelSignalSema(q->lock, 1);
    /* Transfer ownership.  av_read_frame packets already use the owning
     * destructor; duplicating the struct and then freeing the source would
     * invalidate the queued payload. */
    packet->data = NULL;
    packet->size = 0;
    packet->destruct = NULL;
    return 0;
}

/* 1=packet, 0=end, -1=abort. */
static int packet_queue_get(PlayerPacketQueue *q, AVPacket *packet)
{
    for (;;) {
        PlayerPacketNode *node = NULL;
        if (q->abort || player_cancel) return -1;
        if (sceKernelWaitSema(q->lock, 1, NULL) < 0) return -1;
        if (q->head) {
            node = q->head;
            q->head = node->next;
            if (!q->head) q->tail = NULL;
            q->bytes -= node->packet.size;
        }
        sceKernelSignalSema(q->lock, 1);
        if (node) {
            *packet = node->packet;
            free(node);
            return 1;
        }
        if (q->ended) return 0;
        sceKernelDelayThreadCB(1000);
    }
}

static void packet_queue_destroy(PlayerPacketQueue *q)
{
    PlayerPacketNode *node, *next;
    q->abort = 1;
    node = q->head;
    while (node) {
        next = node->next;
        av_free_packet(&node->packet);
        free(node);
        node = next;
    }
    q->head = q->tail = NULL;
    if (q->lock >= 0) sceKernelDeleteSema(q->lock);
    q->lock = -1;
}


static void release_audio_src(void)
{
    int retries = 100;
    /* Blocking output returns when the buffer has been submitted; hardware may
     * still be draining its final samples before the SRC channel is releasable. */
    sceKernelDelayThread(50000);
    while (sceAudioSRCChRelease() < 0 && --retries > 0)
        sceKernelDelayThread(10000);
}

static void publish_yuv420(const AVFrame *frame, int width, int height)
{
    int y_stride = frame->linesize[0];
    int uv_height = (height + 1) >> 1;
    int uv_stride = frame->linesize[1];
    if (width > Y_STRIDE_MAX || height > Y_HEIGHT_MAX) return;
    if (!frame->data[0] || !frame->data[1] || !frame->data[2] ||
        y_stride <= 0 || uv_stride <= 0 || frame->linesize[2] != uv_stride) return;
    frame_rgba_ptr = NULL;
    frame_y_ptr = frame->data[0];
    frame_v_ptr = frame->data[2];
    frame_u_ptr = frame->data[1];
    sceKernelDcacheWritebackRange(frame_y_ptr, y_stride * height);
    sceKernelDcacheWritebackRange(frame_v_ptr, uv_stride * uv_height);
    sceKernelDcacheWritebackRange(frame_u_ptr, uv_stride * uv_height);
    frame_width = width;
    frame_height = height;
    frame_y_stride = y_stride;
    frame_uv_stride = uv_stride;
    frame_index = 0;
    player_frames_decoded++;
}

static void publish_rgba(const unsigned char *rgba, int width, int height,
                         int stride)
{
    frame_rgba_ptr = rgba;
    frame_rgba_stride = stride;
    frame_width = width;
    frame_height = height;
    frame_index = 0;
    player_frames_decoded++;
}

static int stream_read_packet(void *opaque, uint8_t *buffer, int size)
{
    if (player_cancel) return -1;
    return go_http_stream_read(opaque, buffer, size);
}

#if LIBAVFORMAT_VERSION_MAJOR >= 53
typedef int64_t GoStreamOffset;
#else
typedef offset_t GoStreamOffset;
#endif

static GoStreamOffset stream_seek_packet(void *opaque, GoStreamOffset offset,
                                         int whence)
{
    if (player_cancel) return -1;
    return (GoStreamOffset)go_http_stream_seek(opaque, offset, whence);
}

static int video_decode_worker(SceSize args, void *argp)
{
    PlayerPipeline *pipeline = *(PlayerPipeline **)argp;
    AVPacket packet;
    int64_t first_pts = (int64_t)AV_NOPTS_VALUE;
    uint64_t clock_start = 0;
    (void)args;
    while (packet_queue_get(&pipeline->video_q, &packet) == 1) {
        int left = packet.size, got_picture = 0;
        unsigned char *data = packet.data;
        int64_t stamp = packet.pts != (int64_t)AV_NOPTS_VALUE ? packet.pts : packet.dts;
        if (stamp != (int64_t)AV_NOPTS_VALUE) {
            uint64_t now = sceKernelGetSystemTimeWide();
            /* Audio output is blocking, but it does not pace this independent
             * video worker.  The former audio-present branch therefore
             * published H.264 frames as quickly as the CPU decoded them (the
             * smoke test advanced 83 seconds in 30), producing bursty motion
             * and starving the other player threads.  PTS is the presentation
             * clock in both cases; pace video to it whether audio exists or
             * not.  When decoding falls behind, wait is negative and frames
             * continue immediately, so this adds no penalty to a slow PSP. */
            if (first_pts == (int64_t)AV_NOPTS_VALUE) {
                first_pts = stamp;
                clock_start = now;
                player_time_value = 100;
            } else {
                player_time_value = (int)((stamp - first_pts) *
                                    av_q2d(pipeline->video_time_base) * 100.0) + 100;
                int64_t wait = (int64_t)((stamp - first_pts) *
                               av_q2d(pipeline->video_time_base) * 1000000.0) -
                               (int64_t)(now - clock_start);
                if (wait > 0) sceKernelDelayThread((unsigned int)wait);
            }
        }
        if (pipeline->hardware_avc && go_avc_hw_active()) {
            const unsigned char *rgba = NULL;
            int stride = 0;
            int hw_result = go_avc_hw_decode(packet.data, packet.size,
                                             &rgba, &stride);
            if (hw_result > 0)
                publish_rgba(rgba, pipeline->video->width,
                             pipeline->video->height, stride);
            if (hw_result >= 0) {
                av_free_packet(&packet);
                continue;
            }
            pipeline->hardware_avc = 0;
            go_modern_trace("AVC_HW falling back to FFmpeg");
        }
        while (left > 0 && !player_cancel) {
            int used;
            got_picture = 0;
            sceKernelWaitSema(pipeline->codec_lock, 1, NULL);
#if LIBAVCODEC_VERSION_MAJOR >= 53
            {
                AVPacket decode_packet;
                av_init_packet(&decode_packet);
                decode_packet.data = data;
                decode_packet.size = left;
                used = avcodec_decode_video2(pipeline->video, pipeline->picture,
                                             &got_picture, &decode_packet);
            }
#else
            used = avcodec_decode_video(pipeline->video, pipeline->picture,
                                        &got_picture, data, left);
#endif
            sceKernelSignalSema(pipeline->codec_lock, 1);
            if (used <= 0) break;
            data += used;
            left -= used;
            if (got_picture && pipeline->video->pix_fmt == PIX_FMT_YUV420P)
                publish_yuv420(pipeline->picture, pipeline->video->width,
                               pipeline->video->height);
        }
        av_free_packet(&packet);
    }
    sceKernelExitThread(0);
    return 0;
}

static int audio_decode_worker(SceSize args, void *argp)
{
    PlayerPipeline *pipeline = *(PlayerPipeline **)argp;
    AVPacket packet;
    (void)args;
    /* Packets in this queue are already stereo PCM.  AAC decoding remains in
     * the demux worker where the reconstructed FAAD path was proven on real
     * hardware; only the blocking device write runs independently. */
    while (packet_queue_get(&pipeline->audio_q, &packet) == 1) {
        if (pipeline->audio_reserved &&
            packet.size == pipeline->audio_frame_samples * 2 * (int)sizeof(short))
            sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, packet.data);
        av_free_packet(&packet);
    }
    sceKernelExitThread(0);
    return 0;
}

static void decode_and_queue_audio(PlayerPipeline *pipeline,
                                   AVCodecContext *audio, AVPacket *packet,
                                   short *audio_buffer, short *stereo_buffer)
{
    int left = packet->size;
    unsigned char *data = packet->data;
    while (left > 0 && !player_cancel) {
        int audio_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
        int used;
#if LIBAVCODEC_VERSION_MAJOR >= 53
        AVPacket decode_packet;
        av_init_packet(&decode_packet);
        decode_packet.data = data;
        decode_packet.size = left;
        used = avcodec_decode_audio3(audio, audio_buffer, &audio_size,
                                     &decode_packet);
#else
        used = avcodec_decode_audio2(audio, audio_buffer, &audio_size,
                                     data, left);
#endif
        if (used <= 0) break;
        data += used;
        left -= used;
        if (audio_size > 0 && pipeline->audio_reserved) {
            int samples = audio_size / (audio->channels * (int)sizeof(short));
            short *output = audio_buffer;
            AVPacket pcm;
            if (audio->channels == 1) {
                int n;
                for (n = 0; n < samples; n++) {
                    stereo_buffer[n * 2] = audio_buffer[n];
                    stereo_buffer[n * 2 + 1] = audio_buffer[n];
                }
                output = stereo_buffer;
            }
            if (samples == pipeline->audio_frame_samples &&
                (audio->channels == 1 || audio->channels == 2) &&
                av_new_packet(&pcm, samples * 2 * (int)sizeof(short)) >= 0) {
                memcpy(pcm.data, output, pcm.size);
                if (packet_queue_put(&pipeline->audio_q, &pcm) < 0)
                    av_free_packet(&pcm);
            }
        }
    }
}

static void close_remote_format(AVFormatContext *format,
                                unsigned char **io_buffer)
{
    int flags;
    if (!format) return;
    flags = format->iformat->flags;
    format->iformat->flags |= AVFMT_NOFILE;
    av_close_input_file(format);
    format->iformat->flags = flags;
    if (io_buffer && *io_buffer) {
        av_free(*io_buffer);
        *io_buffer = NULL;
    }
}

static int decode_file(const char *path, const char *audio_path, int remote)
{
    AVFormatContext *format = NULL, *audio_format = NULL;
    AVCodecContext *video = NULL, *audio = NULL;
    AVCodec *codec;
    AVFrame *picture = NULL;
    AVPacket packet;
    int video_stream = -1, audio_stream = -1, audio_reserved = 0;
    int audio_frame_samples = 1152;
    int i;
    short *audio_buffer = NULL;
    short *stereo_buffer = NULL;
    void *http_stream = NULL, *audio_http_stream = NULL;
    unsigned char *io_buffer = NULL, *audio_io_buffer = NULL;
    ByteIOContext io, audio_io;
    PlayerPipeline pipeline;
    PlayerPipeline *pipeline_arg = &pipeline;
    SceUID video_thread = -1, audio_thread = -1;

    memset(&pipeline, 0, sizeof(pipeline));
    pipeline.video_q.lock = pipeline.audio_q.lock = -1;
    pipeline.codec_lock = -1;

    av_register_all();
    go_modern_trace("PLAYER demux begin remote=%d adaptive=%d", remote,
                    audio_path && audio_path[0]);
    if (remote) {
        AVInputFormat *input = (strstr(path, "itag=18") ||
                                strstr(path, "itag=160")) ?
            av_find_input_format("mov,mp4,m4a,3gp,3g2,mj2") :
            av_find_input_format("flv");
        http_stream = go_http_stream_open(path);
        io_buffer = av_malloc(32768);
        if (!http_stream || !io_buffer || !input) goto fail;
        init_put_byte(&io, io_buffer, 32768, 0, http_stream,
                      stream_read_packet, NULL, stream_seek_packet);
        /* DASH/fMP4 carries initialization and successive moof/mdat fragments
         * in presentation order.  Advertising it as a random-access file
         * makes the MOV probe scan the multi-megabyte tail before playback,
         * which cannot fit in the PSP's bounded transport ring. */
        io.is_streamed = audio_path && audio_path[0] ? 1 : 0;
#if LIBAVFORMAT_VERSION_MAJOR >= 53
        if (audio_path && audio_path[0]) io.seekable = 0;
#endif
        if (av_open_input_stream(&format, &io, path, input, NULL) < 0) goto fail;
        if (audio_path && audio_path[0]) {
            audio_http_stream = go_http_stream_open(audio_path);
            audio_io_buffer = av_malloc(32768);
            if (!audio_http_stream || !audio_io_buffer) goto fail;
            init_put_byte(&audio_io, audio_io_buffer, 32768, 0,
                          audio_http_stream, stream_read_packet, NULL,
                          stream_seek_packet);
            audio_io.is_streamed = 1;
#if LIBAVFORMAT_VERSION_MAJOR >= 53
            audio_io.seekable = 0;
#endif
            if (av_open_input_stream(&audio_format, &audio_io, audio_path,
                                     input, NULL) < 0) goto fail;
        }
    } else if (av_open_input_file(&format, path, NULL, 0, NULL) < 0) goto fail;
    if (av_find_stream_info(format) < 0) {
        go_modern_trace("PLAYER stream_info failed");
        goto fail;
    }
    if (audio_format && av_find_stream_info(audio_format) < 0) {
        go_modern_trace("PLAYER audio stream_info failed");
        goto fail;
    }
    go_modern_trace("PLAYER stream_info ok streams=%d", (int)format->nb_streams);
    if (format->duration > 0)
        player_duration_value = (int)(format->duration / (AV_TIME_BASE / 100));
    for (i = 0; i < (int)format->nb_streams; i++) {
        if (format->streams[i]->codec->codec_type ==
#if LIBAVCODEC_VERSION_MAJOR >= 53
            AVMEDIA_TYPE_VIDEO
#else
            CODEC_TYPE_VIDEO
#endif
            && video_stream < 0)
            video_stream = i;
        if (!audio_format &&
            format->streams[i]->codec->codec_type ==
#if LIBAVCODEC_VERSION_MAJOR >= 53
            AVMEDIA_TYPE_AUDIO
#else
            CODEC_TYPE_AUDIO
#endif
            && audio_stream < 0)
            audio_stream = i;
    }
    if (audio_format) {
        for (i = 0; i < (int)audio_format->nb_streams; i++)
            if (audio_format->streams[i]->codec->codec_type ==
#if LIBAVCODEC_VERSION_MAJOR >= 53
                AVMEDIA_TYPE_AUDIO
#else
                CODEC_TYPE_AUDIO
#endif
                ) {
                audio_stream = i;
                break;
            }
    }
    if (video_stream >= 0) {
        video = format->streams[video_stream]->codec;
        /* FUN_00022764 codec context contract. */
        video->workaround_bugs = 1;
#if LIBAVCODEC_VERSION_MAJOR < 53
        video->error_resilience = 1;
#else
        video->error_recognition = 1;
#endif
        video->error_concealment = 3;
        video->thread_count = 1;
        /* Decode YouTube's 640x360 stream directly to 320x180.  Scaling a
         * fully reconstructed frame afterwards saves no decoder work; the
         * H.264 lowres path reduces inverse transforms, motion compensation,
         * cache traffic and GU texture upload while retaining enough detail
         * for the PSP's 480x272 panel. */
        video->lowres = remote && strstr(path, "itag=18") ? 1 : 0;
        video->idct_algo = 0;
        /* Current YouTube's progressive stream is 640x360 H.264 at 30 fps,
         * considerably heavier than the formats GoTube 1.2 targeted.  The
         * 2007 decoder exposes its intended low-power H.264 path through
         * FAST plus deblocking discard.  Deblocking is presentation polish,
         * not required for correct reference reconstruction, and is one of
         * the largest pure-CPU costs on Allegrex.  Preserve exact legacy/local
         * playback settings; apply this only to modern remote MP4. */
        if (remote && (strstr(path, "itag=18") || audio_format)) {
            video->flags2 |= CODEC_FLAG2_FAST;
            video->skip_loop_filter = AVDISCARD_ALL;
        } else {
            video->skip_loop_filter = AVDISCARD_NONE;
        }
        video->debug = 0;
        video->debug_mv = 0;
        video->skip_idct = AVDISCARD_NONE;
        video->skip_frame = AVDISCARD_NONE;
        codec = avcodec_find_decoder(video->codec_id);
        if (!codec || avcodec_open(video, codec) < 0) video = NULL;
        else {
            picture = avcodec_alloc_frame();
            go_modern_trace("PLAYER video codec=%d size=%dx%d fast=%d lowres=%d",
                            video->codec_id, video->width, video->height,
                            !!(video->flags2 & CODEC_FLAG2_FAST), video->lowres);
            if (audio_format && video->codec_id == CODEC_ID_H264 &&
                go_avc_hw_init(video->extradata, video->extradata_size,
                               video->width, video->height) == 0)
                pipeline.hardware_avc = 1;
        }
    }
    if (audio_stream >= 0) {
        audio = (audio_format ? audio_format : format)->streams[audio_stream]->codec;
        codec = avcodec_find_decoder(audio->codec_id);
        if (!codec || avcodec_open(audio, codec) < 0) audio = NULL;
        else {
            audio_buffer = memalign(64, AVCODEC_MAX_AUDIO_FRAME_SIZE);
            stereo_buffer = memalign(64, AVCODEC_MAX_AUDIO_FRAME_SIZE * 2);
            if (!audio_buffer || !stereo_buffer) goto fail;
            if (audio->frame_size > 0) audio_frame_samples = audio->frame_size;
            audio_reserved = sceAudioSRCChReserve(audio_frame_samples,
                                                  audio->sample_rate, 2) >= 0;
            go_modern_trace("PLAYER audio codec=%d rate=%d channels=%d frame=%d reserved=%d",
                            audio->codec_id, audio->sample_rate,
                            audio->channels, audio_frame_samples,
                            audio_reserved);
        }
    }
    if (!video && !audio) goto fail;
    pipeline.video = video;
    pipeline.audio = audio;
    pipeline.picture = picture;
    pipeline.audio_reserved = audio_reserved;
    pipeline.audio_frame_samples = audio_frame_samples;
    if (video) pipeline.video_time_base = format->streams[video_stream]->time_base;
    packet_queue_init(&pipeline.video_q, "GoTube.video.queue");
    packet_queue_init(&pipeline.audio_q, "GoTube.audio.queue");
    pipeline.codec_lock = sceKernelCreateSema("GoTube.codec.lock", 0, 1, 1, NULL);
    if ((video && pipeline.video_q.lock < 0) || (audio && pipeline.audio_q.lock < 0)) goto fail;
    if (pipeline.codec_lock < 0) goto fail;
    if (video) {
        /* Original video worker at 0x231c4. */
        video_thread = sceKernelCreateThread("GoTube.video", video_decode_worker,
                                             0x22, 0x10000,
                                             PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, NULL);
        if (video_thread < 0 || sceKernelStartThread(video_thread, sizeof(pipeline_arg),
                                                     &pipeline_arg) < 0) goto fail;
    }
    if (audio) {
        /* Original audio worker at 0x25424. */
        /* Original PCM/device worker contract at 0x25424. */
        audio_thread = sceKernelCreateThread("GoTube.audio", audio_decode_worker,
                                             0x1f, 0x10000,
                                             PSP_THREAD_ATTR_USER, NULL);
        if (audio_thread < 0 || sceKernelStartThread(audio_thread, sizeof(pipeline_arg),
                                                     &pipeline_arg) < 0) goto fail;
    }
    player_status = 2;
    if (audio_format) {
        int video_eof = 0, audio_eof = 0;
        while (!player_cancel && (!video_eof || !audio_eof)) {
            if (player_pause && !player_cancel) {
                while (player_pause && !player_cancel)
                    sceKernelDelayThread(10000);
            }
            if (!video_eof) {
                if (av_read_frame(format, &packet) < 0) video_eof = 1;
                else {
                    if (video && packet.stream_index == video_stream) {
                        if (packet_queue_put(&pipeline.video_q, &packet) < 0)
                            av_free_packet(&packet);
                    }
                    av_free_packet(&packet);
                }
            }
            if (!audio_eof && !player_cancel) {
                if (av_read_frame(audio_format, &packet) < 0) audio_eof = 1;
                else {
                    if (audio && packet.stream_index == audio_stream)
                        decode_and_queue_audio(&pipeline, audio, &packet,
                                               audio_buffer, stereo_buffer);
                    av_free_packet(&packet);
                }
            }
        }
    } else while (!player_cancel && av_read_frame(format, &packet) >= 0) {
        if (player_pause && !player_cancel) {
            while (player_pause && !player_cancel)
                sceKernelDelayThread(10000);
        }
        if (player_cancel) {
            av_free_packet(&packet);
            break;
        }
        if (video && packet.stream_index == video_stream) {
            if (packet_queue_put(&pipeline.video_q, &packet) < 0) av_free_packet(&packet);
        } else if (audio && packet.stream_index == audio_stream) {
            decode_and_queue_audio(&pipeline, audio, &packet,
                                   audio_buffer, stereo_buffer);
        }
        av_free_packet(&packet);
    }
    packet_queue_finish(&pipeline.video_q);
    packet_queue_finish(&pipeline.audio_q);
    if (video_thread >= 0) { sceKernelWaitThreadEnd(video_thread, NULL); sceKernelDeleteThread(video_thread); }
    if (audio_thread >= 0) { sceKernelWaitThreadEnd(audio_thread, NULL); sceKernelDeleteThread(audio_thread); }
    packet_queue_destroy(&pipeline.video_q);
    packet_queue_destroy(&pipeline.audio_q);
    if (pipeline.codec_lock >= 0) { sceKernelDeleteSema(pipeline.codec_lock); pipeline.codec_lock = -1; }
    if (audio_reserved) release_audio_src();
    if (audio_buffer) free(audio_buffer);
    if (stereo_buffer) free(stereo_buffer);
    if (picture) av_free(picture);
    go_avc_hw_shutdown();
    if (video) avcodec_close(video);
    if (audio) avcodec_close(audio);
    if (audio_format) close_remote_format(audio_format, &audio_io_buffer);
    if (format) {
        if (remote) close_remote_format(format, &io_buffer);
        else av_close_input_file(format);
    }
    if (audio_http_stream) go_http_stream_close(audio_http_stream);
    if (http_stream) go_http_stream_close(http_stream);
    return player_cancel ? -1 : 0;

fail:
    go_modern_trace("PLAYER decode failed status=%d", player_status);
    packet_queue_abort(&pipeline.video_q);
    packet_queue_abort(&pipeline.audio_q);
    if (video_thread >= 0) { sceKernelWaitThreadEnd(video_thread, NULL); sceKernelDeleteThread(video_thread); }
    if (audio_thread >= 0) { sceKernelWaitThreadEnd(audio_thread, NULL); sceKernelDeleteThread(audio_thread); }
    packet_queue_destroy(&pipeline.video_q);
    packet_queue_destroy(&pipeline.audio_q);
    if (pipeline.codec_lock >= 0) { sceKernelDeleteSema(pipeline.codec_lock); pipeline.codec_lock = -1; }
    if (audio_reserved) release_audio_src();
    if (audio_buffer) free(audio_buffer);
    if (stereo_buffer) free(stereo_buffer);
    if (picture) av_free(picture);
    go_avc_hw_shutdown();
    if (video) avcodec_close(video);
    if (audio) avcodec_close(audio);
    if (audio_format) close_remote_format(audio_format, &audio_io_buffer);
    if (format) {
        if (remote) close_remote_format(format, &io_buffer);
        else av_close_input_file(format);
    }
    if (audio_io_buffer) av_free(audio_io_buffer);
    if (io_buffer) av_free(io_buffer);
    if (audio_http_stream) go_http_stream_close(audio_http_stream);
    if (http_stream) go_http_stream_close(http_stream);
    return -1;
}

static int player_worker(SceSize args, void *argp)
{
    (void)args; (void)argp;
    player_status = 1;
    player_status = decode_file(player_url,
                                player_audio_url[0] ? player_audio_url : NULL,
                                !player_local_file) < 0 ? -2 : 3;
    sceKernelExitThread(0);
    return 0;
}

int go_player_start(const char *url)
{
    if (!url || !url[0] ||
        ((player_status == 1 || player_status == 2) && !player_cancel)) return -1;
    if (player_thread >= 0) {
        sceKernelWaitThreadEnd(player_thread, NULL);
        sceKernelDeleteThread(player_thread);
        player_thread = -1;
    }
    strncpy(player_url, url, sizeof(player_url) - 1);
    player_url[sizeof(player_url) - 1] = 0;
    player_audio_url[0] = 0;
    player_local_file = 0;
    player_cancel = 0;
    player_pause = 0;
    player_progress_value = 0;
    frame_index = -1;
    frame_rgba_ptr = NULL;
    player_frames_decoded = 0;
    player_time_value = 0;
    player_duration_value = 0;
    /* FUN_00021e54: original decoder worker priority 0x22, stack 0x40000,
     * attributes 0x80004000 (USER | VFPU).  H.264 Main decoding exceeds the
     * smaller reconstruction stack after its first delayed frame. */
    player_thread = sceKernelCreateThread("GoTube.player", player_worker,
                                          0x22, 0x40000,
                                          PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, NULL);
    if (player_thread < 0) return player_thread;
    player_status = 1;
    {
        int ret = sceKernelStartThread(player_thread, 0, NULL);
        if (ret < 0) {
            sceKernelDeleteThread(player_thread);
            player_thread = -1;
            player_status = -1;
        }
        return ret;
    }
}

int go_player_start_adaptive(const char *video_url, const char *audio_url)
{
    int ret;
    if (!video_url || !video_url[0] || !audio_url || !audio_url[0] ||
        ((player_status == 1 || player_status == 2) && !player_cancel))
        return -1;
    if (player_thread >= 0) {
        sceKernelWaitThreadEnd(player_thread, NULL);
        sceKernelDeleteThread(player_thread);
        player_thread = -1;
    }
    snprintf(player_url, sizeof(player_url), "%s", video_url);
    snprintf(player_audio_url, sizeof(player_audio_url), "%s", audio_url);
    player_local_file = 0;
    player_cancel = 0;
    player_pause = 0;
    player_progress_value = 0;
    frame_index = -1;
    frame_rgba_ptr = NULL;
    player_frames_decoded = 0;
    player_time_value = 0;
    player_duration_value = 0;
    player_thread = sceKernelCreateThread("GoTube.player", player_worker,
                                          0x22, 0x40000,
                                          PSP_THREAD_ATTR_USER |
                                          PSP_THREAD_ATTR_VFPU, NULL);
    if (player_thread < 0) return player_thread;
    player_status = 1;
    ret = sceKernelStartThread(player_thread, 0, NULL);
    if (ret < 0) {
        sceKernelDeleteThread(player_thread);
        player_thread = -1;
        player_status = -1;
    }
    return ret;
}

int go_player_start_file(const char *path)
{
    int ret;
    if (!path || !path[0] ||
        ((player_status == 1 || player_status == 2) && !player_cancel)) return -1;
    if (player_thread >= 0) {
        sceKernelWaitThreadEnd(player_thread, NULL);
        sceKernelDeleteThread(player_thread);
        player_thread = -1;
    }
    strncpy(player_url, path, sizeof(player_url) - 1);
    player_url[sizeof(player_url) - 1] = 0;
    player_audio_url[0] = 0;
    player_local_file = 1;
    player_cancel = 0;
    player_pause = 0;
    player_progress_value = 1000;
    frame_index = -1;
    frame_rgba_ptr = NULL;
    player_frames_decoded = 0;
    player_time_value = 0;
    player_duration_value = 0;
    go_comments_load_for_media(path);
    player_thread = sceKernelCreateThread("GoTube.player", player_worker,
                                          0x22, 0x40000,
                                          PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, NULL);
    if (player_thread < 0) return player_thread;
    player_status = 1;
    ret = sceKernelStartThread(player_thread, 0, NULL);
    if (ret < 0) {
        sceKernelDeleteThread(player_thread);
        player_thread = -1;
        player_status = -1;
    }
    return ret;
}

void go_player_stop(void)
{
    go_modern_trace("PLAYER stop frames=%d time=%d duration=%d",
                    player_frames_decoded, player_time_value,
                    player_duration_value);
    player_cancel = 1;
    player_pause = 0;
}

void go_player_toggle_pause(void)
{
    if (player_status == 2) player_pause = !player_pause;
}

void go_player_cycle_overlay(void)
{
    player_overlay_mode = (player_overlay_mode + 1) % 5;
}

void go_player_cycle_render_mode(void)
{
    player_render_mode = (player_render_mode + 1) % 15;
}

void go_player_cycle_speed(int direction)
{
    if (direction < 0) {
        if (player_speed_mode == 20) player_speed_mode = 10;
        else if (player_speed_mode == 10) player_speed_mode = 5;
    } else {
        if (player_speed_mode == 5) player_speed_mode = 10;
        else if (player_speed_mode == 10) player_speed_mode = 20;
    }
}

int go_player_overlay_mode(void) { return player_overlay_mode; }
int go_player_render_mode(void) { return player_render_mode; }
int go_player_speed_mode(void) { return player_speed_mode; }
void go_player_set_render_mode(int mode)
{
    player_render_mode = mode >= 0 ? mode % 15 : 0;
}

int go_player_paused(void) { return player_pause; }

int go_player_state(void) { return player_status; }
int go_player_progress(void) { return player_progress_value; }
int go_player_time_cs(void) { return player_time_value; }
int go_player_duration_cs(void) { return player_duration_value; }
void go_player_set_source_url(const char *url)
{
    snprintf(player_source_url, sizeof(player_source_url), "%s", url ? url : "");
}
int go_player_matches_source(const char *url)
{
    return url && url[0] && player_source_url[0] &&
           (player_status == 1 || player_status == 2) &&
           strcmp(url, player_source_url) == 0;
}

int go_player_planes(const unsigned char **y, const unsigned char **v,
                     const unsigned char **u, int *width, int *height,
                     int *y_stride, int *uv_stride)
{
    int index = frame_index;
    if (index < 0 || frame_rgba_ptr) return 0;
    if (y) *y = frame_y_ptr;
    if (v) *v = frame_v_ptr;
    if (u) *u = frame_u_ptr;
    if (width) *width = frame_width;
    if (height) *height = frame_height;
    if (y_stride) *y_stride = frame_y_stride;
    if (uv_stride) *uv_stride = frame_uv_stride;
    return 1;
}

int go_player_rgba(const unsigned char **rgba, int *width, int *height,
                   int *stride)
{
    if (frame_index < 0 || !frame_rgba_ptr) return 0;
    if (rgba) *rgba = frame_rgba_ptr;
    if (width) *width = frame_width;
    if (height) *height = frame_height;
    if (stride) *stride = frame_rgba_stride;
    return 1;
}
