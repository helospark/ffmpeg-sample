/*
 * Copyright (c) 2017 Jun Zhao
 * Copyright (c) 2017 Kaixuan Liu
 *
 * HW Acceleration API (video decoding) decode sample
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

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


const char *filter_descr = "scale_vaapi=400:300,hwdownload,format=yuv420p";

static AVBufferRef *hw_device_ctx = NULL;
static enum AVPixelFormat hw_pix_fmt;
AVPixelFormat FORMAT = AV_PIX_FMT_RGB24;

int imageNumber = 0;
char buf[200];

AVCodecContext *decoder_ctx;

AVFilterContext *buffersink_ctx;
AVFilterContext *buffersrc_ctx;
AVFilterGraph *filter_graph = NULL;

AVBufferRef* real_hw_device_ctx = NULL;

struct SwsContext* sws_ctx;

static int hw_decoder_init(AVCodecContext *ctx, const enum AVHWDeviceType type)
{
    int err = 0;

    AVBufferRef *asd = NULL;

    if ((err = av_hwdevice_ctx_create(&asd, type,
                                      "/dev/dri/renderD129", NULL, 0)) < 0) {
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

static int createInputFilter(AVCodecContext* dec_ctx, AVFilterInOut *inputs) {
    int ret;
    char args[512];
    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");

    snprintf(args, sizeof(args),
        "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
        dec_ctx->width, dec_ctx->height, hw_pix_fmt,
        1, 60,
        dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);
    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        return ret;
    }
    AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();
    

    if (!par)
        return AVERROR(ENOMEM);
    memset(par, 0, sizeof(*par));
    par->format = AV_PIX_FMT_NONE;
    par->hw_frames_ctx = real_hw_device_ctx;
    ret = av_buffersrc_parameters_set(buffersrc_ctx, par);
    av_freep(&par);
    
    if ((ret = avfilter_link(buffersrc_ctx, 0, inputs[0].filter_ctx, inputs[0].pad_idx)) < 0)
        return ret;
    return 0;
}


static int createOutputFilter(AVCodecContext* dec_ctx, AVFilterInOut *outputs) {
    int ret;
    char args[512];
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");

    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                       NULL, NULL, filter_graph);
    
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        return ret;
    }


    if ((ret = avfilter_link(outputs[0].filter_ctx, 0, buffersink_ctx, 0)) < 0)
        return ret;
    return 0;
}

static int init_filters(const char *filters_descr, AVCodecContext* dec_ctx)
{
    char args[512];
    int ret;
    AVFilterInOut *outputs = NULL;
    AVFilterInOut *inputs  = NULL;

    filter_graph = avfilter_graph_alloc();

    if ((ret = avfilter_graph_parse2(filter_graph, filters_descr,
                                    &inputs, &outputs)) < 0)
        return ret;

    for (int i = 0; i < filter_graph->nb_filters; ++i) {
            filter_graph->filters[i]->hw_device_ctx = av_buffer_ref(real_hw_device_ctx);
    }

    createInputFilter(dec_ctx, inputs);
    createOutputFilter(dec_ctx, outputs);



    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        return ret;
    return 0;
}



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

            if (filter_graph == NULL && init_filters(filter_descr, decoder_ctx) < 0) {
                av_log(NULL, AV_LOG_ERROR, "Init filter fails\n");

                return -1;
            }

            // push the decoded frame into the filtergraph
            if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
                break;
            }
            // pull filtered frames from the filtergraph
            while (1) {
                filt_frame = av_frame_alloc();
                ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error reading from buffersink\n");
                    break;
                }
                
                imageNumber += 1;

                AVFrame* pFrameRGB=allocateFrame(400, 300, FORMAT);
                sws_scale(sws_ctx, (uint8_t const * const *)filt_frame->data,
                        filt_frame->linesize, 0, filt_frame->height,
                        pFrameRGB->data, pFrameRGB->linesize);

                if (imageNumber % 100 == 0) {
                    snprintf(buf, sizeof(buf), "/tmp/%s_%03d.ppm", "hwdecode", imageNumber);
                    ppm_save(pFrameRGB->data[0], pFrameRGB->linesize[0], pFrameRGB->width, pFrameRGB->height, buf);
                }



                av_frame_free(&filt_frame);
            }


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
    AVStream *video = NULL;
    decoder_ctx = NULL;
    AVCodec *decoder = NULL;
    AVPacket packet;
    enum AVHWDeviceType type;
    int i;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input file> <device type (ex. vaapi)>\n", argv[0]);
        return -1;
    }
    const char* typeName = argv[2];
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

    if (hw_decoder_init(decoder_ctx, type) < 0)
        return -1;


    decoder_ctx->thread_count = 0; // 0 = automatic
    decoder_ctx->thread_type = (FF_THREAD_FRAME | FF_THREAD_SLICE);

    if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream);
        return -1;
    }


   long long start = time(NULL);

    long frames = 0;

    sws_ctx = sws_getContext(   400,
                                300,
                                AV_PIX_FMT_YUV420P,
                                400,
                                300,
                                FORMAT,
                                SWS_FAST_BILINEAR,
                                NULL,
                                NULL,
                                NULL
                            );

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