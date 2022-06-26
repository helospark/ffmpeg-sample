/**
 * @file
 * HW-Accelerated decoding example.
 *
 * @example hw_decode.c
 * This example shows how to do HW-accelerated decoding with output
 * frames from the HW video surfaces.
 */

#include <stdio.h>
#include "debugimage.h"

#include <cassert>
extern "C" {
#include "helper.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavformat/avformat.h"
#include "libavfilter/buffersrc.h"
#include "libavutil/pixdesc.h"
#include "libavutil/hwcontext.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavutil/fifo.h"

static AVBufferRef *hw_device_ctx = NULL;
static enum AVPixelFormat hw_pix_fmt;
AVPixelFormat FORMAT = AV_PIX_FMT_RGB24;

int width=400;
int height=300;

int imageNumber = 0;
char buf[200];

AVCodecContext *decoder_ctx;

AVBufferRef* real_hw_device_ctx = NULL;

struct SwsContext* sws_ctx;
    AVStream *video = NULL;

static int hw_decoder_init(AVCodecContext *ctx, const enum AVHWDeviceType type, const char* device)
{
    int err = 0;

    AVBufferRef *asd = NULL;

    if ((err = av_hwdevice_ctx_create(&asd, type,
                                      device, NULL, 0)) < 0) {
        fprintf(stderr, "Failed to create specified HW device.\n");
        return err;
    }
    ctx->hw_device_ctx = av_buffer_ref(asd);
    
    return err;
}


static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                        const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;

    for (p = pix_fmts; *p != -1; p++) {
        if (*p == hw_pix_fmt)
            return *p;
    }

    fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

    bool containsFormat(AVPixelFormat* formats, AVPixelFormat format) {
        for (int i = 0; formats[i] != AV_PIX_FMT_NONE; i++) {
            if (formats[i] == format) {
                return true;
            }
        }
        return false;
    }

    AVPixelFormat getScaleCapability(AVHWDeviceType type, AVBufferRef* hwBuffer) {
        AVPixelFormat *formats;
        
        int err = av_hwframe_transfer_get_formats(hwBuffer,
                                          AV_HWFRAME_TRANSFER_DIRECTION_FROM,
                                          &formats, 0);

        if (containsFormat(formats, AV_PIX_FMT_NV12)) {
            return AV_PIX_FMT_NV12;
        } else if (containsFormat(formats, AV_PIX_FMT_RGBA)) {
            return AV_PIX_FMT_RGBA;
        } else if (containsFormat(formats, AV_PIX_FMT_RGB0)) {
            return AV_PIX_FMT_RGB0;
        } else if (containsFormat(formats, AV_PIX_FMT_YUV420P)) {
            return AV_PIX_FMT_YUV420P;
        } else if (containsFormat(formats, AV_PIX_FMT_RGB0)) {
            return AV_PIX_FMT_RGB0;
        } else {
            return AV_PIX_FMT_NONE;
        }


        av_freep(&formats);
        return AV_PIX_FMT_NONE;
    }

AVPixelFormat resultFormat;
    enum AVHWDeviceType type;


static int decode_write(AVCodecContext *avctx, AVPacket *packet)
{
    AVFrame *frame = NULL, *sw_frame = NULL;
    AVFrame *tmp_frame = NULL;
    AVFrame *filt_frame;
    uint8_t *buffer = NULL;
    int size;
    int ret = 0;



    ret = avcodec_send_packet(avctx, packet);
    if (ret < 0) {
        fprintf(stderr, "Error during decoding\n");
        return ret;
    }

    while (1) {
        if (!(frame = av_frame_alloc()) || !(sw_frame = av_frame_alloc())) {
            fprintf(stderr, "Can not alloc frame\n");
            ret = AVERROR(ENOMEM);
            return ret;
        }


        ret = avcodec_receive_frame(avctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&frame);
            av_frame_free(&sw_frame);
            return 0;
        } else if (ret < 0) {
            fprintf(stderr, "Error while decoding\n");
            return ret;
        } else {
            if (real_hw_device_ctx == NULL) {
                real_hw_device_ctx = frame->hw_frames_ctx;
            }

            if ((ret = av_hwframe_transfer_data(sw_frame, frame, 0)) < 0) {
                fprintf(stderr, "Error transferring the data to system memory\n");
                return -1;
            }

            if (sws_ctx == NULL) {
                sws_ctx = sws_getContext(   video->codecpar->width,
                                            video->codecpar->height,
                                            (AVPixelFormat) sw_frame->format,
                                            width,
                                            height,
                                            FORMAT,
                                            SWS_FAST_BILINEAR,
                                            NULL,
                                            NULL,
                                            NULL
                                        );
            }

            tmp_frame = sw_frame;

            AVFrame* pFrameRGB=allocateFrame(width, height, FORMAT);
            sws_scale(sws_ctx, (uint8_t const * const *)sw_frame->data,
                    sw_frame->linesize, 0, sw_frame->height,
                    pFrameRGB->data, pFrameRGB->linesize);
                    
                    
            if (imageNumber % 10 == 0) {
                snprintf(buf, sizeof(buf), "/tmp/%s_%03d.ppm", "hwdecode_without_filters", imageNumber);
                ppm_save(pFrameRGB->data[0], pFrameRGB->linesize[0], pFrameRGB->width, pFrameRGB->height, buf);
            }
            imageNumber += 1;

        }



        av_frame_free(&frame);
        av_frame_free(&sw_frame);

    }
    return 0;
}

int main(int argc, char *argv[])
{
    AVFormatContext *input_ctx = NULL;
    int video_stream, ret;
    decoder_ctx = NULL;
    AVCodec *decoder = NULL;
    AVPacket packet;
    int i;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input file> <device type (ex. vaapi)>\n", argv[0]);
        return -1;
    }
    const char* device = argc >= 2 ? argv[2] : "/dev/dri/renderD128";
    const char* typeName = "vaapi";
    const char* input = argv[1];

   // av_log_set_level(AV_LOG_TRACE);

    type = av_hwdevice_find_type_by_name(typeName);
    if (type == AV_HWDEVICE_TYPE_NONE) {
        fprintf(stderr, "Device type %s is not supported.\n", typeName);
        fprintf(stderr, "Available device types:");
        while((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
            fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
        fprintf(stderr, "\n");
        return -1;
    }

    /* open the input file */
    if (avformat_open_input(&input_ctx, input, NULL, NULL) != 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", input);
        return -1;
    }

    /* find the video stream information */
    ret = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }
    video_stream = ret;

    for (i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
        if (!config) {
            fprintf(stderr, "Decoder %s does not support device type %s.\n",
                    decoder->name, av_hwdevice_get_type_name(type));
            return -1;
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == type) {
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }

    if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
        return AVERROR(ENOMEM);

    video = input_ctx->streams[video_stream];
    if (avcodec_parameters_to_context(decoder_ctx, video->codecpar) < 0)
        return -1;

    decoder_ctx->get_format  = get_hw_format;

    if (hw_decoder_init(decoder_ctx, type, device) < 0)
        return -1;


    decoder_ctx->thread_count = 0; // 0 = automatic
    decoder_ctx->thread_type = (FF_THREAD_FRAME | FF_THREAD_SLICE);

    if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream);
        return -1;
    }



   long long start = time(NULL);

    long frames = 0;


    /* actual decoding and dump the raw data */
    while (ret >= 0) {
      //  fprintf(stdout, "Frame .. \n");
        if ((ret = av_read_frame(input_ctx, &packet)) < 0)
            break;

        if (video_stream == packet.stream_index)
            ret = decode_write(decoder_ctx, &packet);

        av_packet_unref(&packet);
        frames += 1;
        if (frames % 30 == 0)  {
            long long end = time(NULL);
            int took = (end - start);
            fprintf(stdout, "FPS %f\n", frames / (double)took);
        }
    }

    /* flush the decoder */
    packet.data = NULL;
    packet.size = 0;
    ret = decode_write(decoder_ctx, &packet);
    av_packet_unref(&packet);



   long long end = time(NULL);
   int took = (end - start);
   fprintf(stdout, "Took %d\n", took);
   fprintf(stdout, "FPS %f\n", frames / (double)took);


    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&input_ctx);
    av_buffer_unref(&hw_device_ctx);

    return 0;
}
}
