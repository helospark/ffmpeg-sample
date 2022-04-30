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
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>


const char *filter_descr = "scale_vaapi=800:600:format=yuv420p,hwdownload";

static AVBufferRef *hw_device_ctx = NULL;
static enum AVPixelFormat hw_pix_fmt;
AVPixelFormat FORMAT = AV_PIX_FMT_RGB24;

int imageNumber = 0;
char buf[200];

AVCodecContext *decoder_ctx;

AVFilterContext *buffersink_ctx;
AVFilterContext *buffersrc_ctx;
AVFilterGraph *filter_graph;

/**
static int init_filters(AVCodecContext* dec_ctx)
{
    char args[512];
    int ret;
    AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = NULL;
    AVFilterInOut *inputs  = NULL;
    filter_graph = avfilter_graph_alloc();


    if ((ret = avfilter_graph_parse2(filter_graph, filter_descr,
                                    &inputs, &outputs)) < 0)
        return ret;


    for(unsigned i=0; i<filter_graph->nb_filters; i++) {
        filter_graph->filters[i]->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    }




    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_VAAPI_VLD, AV_PIX_FMT_VAAPI, AV_PIX_FMT_NONE };
    AVBufferSinkParams *buffersink_params;
    // buffer video source: the decoded frames from the decoder will be inserted here. 
    snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            dec_ctx->width, dec_ctx->height, hw_pix_fmt,
            805, 48266, //dec_ctx->time_base.num, dec_ctx->time_base.den,
            dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);
    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       args, NULL, filter_graph);


    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        return ret;
    }
    avfilter_link(buffersrc_ctx, 0, inputs->filter_ctx, inputs->pad_idx);



    // buffer video sink: to terminate the filter chain.
    buffersink_params = av_buffersink_params_alloc();
    buffersink_params->pixel_fmts = pix_fmts;
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                       NULL, buffersink_params, filter_graph);

    av_free(buffersink_params);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        return ret;
    }

    avfilter_link(outputs->filter_ctx, 0, buffersink_ctx, 0);



    for(unsigned i=0; i<filter_graph->nb_filters; i++) {
        filter_graph->filters[i]->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    }



    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        return ret;
    return 0;
}
*/


AVFilterContext* m_bufferSrc = nullptr;
AVFilterContext* m_bufferSink = nullptr;
AVFilterContext* m_formatFilter = nullptr;

void initInputFilters(AVFilterInOut* inputs);
void initOutputFilters(AVFilterInOut* outputs);
void filterFrame(AVFrame* inFrame, AVFrame* outFrame);


void initFilters()
{
    AVFilterInOut* inputs = nullptr;
    AVFilterInOut* outputs = nullptr;
    filter_graph = avfilter_graph_alloc();
    assert(avfilter_graph_parse2(filter_graph, "format=vaapi_vld", &inputs, &outputs) == 0);

    for(unsigned i=0; i<filter_graph->nb_filters; i++) {
        filter_graph->filters[i]->hw_device_ctx = av_buffer_ref(hw_device_ctx);
        assert(filter_graph->filters[i]->hw_device_ctx != nullptr);
    }

    initInputFilters(inputs);
    initOutputFilters(outputs);

    assert(avfilter_graph_config(filter_graph, nullptr) == 0);
}
void initInputFilters(AVFilterInOut* inputs)
{
    assert(inputs != nullptr);
    assert(inputs->next == nullptr);

    char args[512];
    snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            decoder_ctx->width, decoder_ctx->height, hw_pix_fmt,
            1, 60,
            1, 1);

    assert(avfilter_graph_create_filter(&m_bufferSrc, avfilter_get_by_name("buffer"), "in",
                                        args, nullptr, filter_graph) == 0);
    assert(avfilter_link(m_bufferSrc, 0, inputs->filter_ctx, inputs->pad_idx) == 0);
}
void initOutputFilters(AVFilterInOut* outputs)
{
    assert(outputs != nullptr);
    assert(outputs->next == nullptr);

    assert(avfilter_graph_create_filter(&m_bufferSink, avfilter_get_by_name("buffersink"), "out",
                                       nullptr, nullptr, filter_graph) == 0);
    assert(avfilter_graph_create_filter(&m_formatFilter, avfilter_get_by_name("format"), "format",
                                       "vaapi_vld", nullptr, filter_graph) == 0);
    assert(avfilter_link(outputs->filter_ctx, outputs->pad_idx, m_formatFilter, 0) == 0);
    assert(avfilter_link(m_formatFilter, 0, m_bufferSink, 0) == 0);
}
void filterFrame(AVFrame* inFrame, AVFrame* outFrame)
{
    assert(av_buffersrc_add_frame_flags(m_bufferSrc, inFrame, AV_BUFFERSRC_FLAG_KEEP_REF) == 0);
    assert(av_buffersink_get_frame(m_bufferSink, outFrame) == 0);
}


static int hw_decoder_init(AVCodecContext *ctx, const enum AVHWDeviceType type)
{
    int err = 0;

    if ((err = av_hwdevice_ctx_create(&hw_device_ctx, type,
                                      "/dev/dri/renderD129", NULL, 0)) < 0) {
        fprintf(stderr, "Failed to create specified HW device.\n");
        return err;
    }
    ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

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
            return;
        }

        ret = avcodec_receive_frame(avctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&frame);
            av_frame_free(&sw_frame);
            return 0;
        } else if (ret < 0) {
            fprintf(stderr, "Error while decoding\n");
            return;
        }

        // push the decoded frame into the filtergraph
        if (av_buffersrc_add_frame_flags(m_bufferSrc, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
            break;
        }
        // pull filtered frames from the filtergraph
        while (1) {
            filt_frame = av_frame_alloc();
            ret = av_buffersink_get_frame(m_bufferSrc, filt_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0)
                return;
            
            imageNumber += 1;

            if (imageNumber % 100 == 0) {
                snprintf(buf, sizeof(buf), "/tmp/%s_%03d.ppm", "hwdecode", imageNumber);
                ppm_save(filt_frame->data[0], filt_frame->linesize[0], filt_frame->width, filt_frame->height, buf);
            }



            av_frame_free(&filt_frame);
            imageNumber += 1;
        }
/*
        if (frame->format == hw_pix_fmt) {
            // retrieve data from GPU to CPU 
            if ((ret = av_hwframe_transfer_data(sw_frame, frame, 0)) < 0) {
                fprintf(stderr, "Error transferring the data to system memory\n");
                goto fail;
            }
            tmp_frame = sw_frame;
        } else {
            tmp_frame = frame;
        }

            size = av_image_get_buffer_size(tmp_frame->format, tmp_frame->width,
                                           tmp_frame->height, 1);
           buffer = av_malloc(size);
           if (!buffer) {
               fprintf(stderr, "Can not alloc buffer\n");
               ret = AVERROR(ENOMEM);
               goto fail;
           }
           ret = av_image_copy_to_buffer(buffer, size,
                                         (const uint8_t * const *)tmp_frame->data,
                                         (const int *)tmp_frame->linesize, tmp_frame->format,
                                         tmp_frame->width, tmp_frame->height, 1);

        imageNumber += 1;


        AVFrame* pFrameRGB=allocateFrame(800, 600, FORMAT);
        sws_scale(sws_ctx, (uint8_t const * const *)tmp_frame->data,
                tmp_frame->linesize, 0, tmp_frame->height,
                pFrameRGB->data, pFrameRGB->linesize);

        if (imageNumber % 100 == 0) {
            snprintf(buf, sizeof(buf), "/tmp/%s_%03d.ppm", "hwdecode", imageNumber);
            ppm_save(pFrameRGB->data[0], pFrameRGB->linesize[0], pFrameRGB->width, pFrameRGB->height, buf);
        }

        av_freep(&pFrameRGB->opaque);
        av_frame_free(&pFrameRGB);
        */

        av_frame_free(&frame);
        av_frame_free(&sw_frame);
        av_freep(&buffer);
        if (ret < 0)
            return ret;
    }
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
        fprintf(stderr, "Usage: %s <device type> <input file>\n", argv[0]);
        return -1;
    }

    av_log_set_level(AV_LOG_TRACE);

    type = av_hwdevice_find_type_by_name(argv[1]);
    if (type == AV_HWDEVICE_TYPE_NONE) {
        fprintf(stderr, "Device type %s is not supported.\n", argv[1]);
        fprintf(stderr, "Available device types:");
        while((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
            fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
        fprintf(stderr, "\n");
        return -1;
    }

    /* open the input file */
    if (avformat_open_input(&input_ctx, argv[2], NULL, NULL) != 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", argv[2]);
        return -1;
    }

    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }

    av_dump_format(input_ctx, 0, argv[2], 0);

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

    initFilters();


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