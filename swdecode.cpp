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


extern "C" {
#include "helper.h"
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>

int imageNumber = 0;
char buf[200];

AVPixelFormat FORMAT = AV_PIX_FMT_RGB24;

static int decode_write(AVCodecContext *avctx, AVPacket *packet, struct SwsContext* sws_ctx)
{
    AVFrame *frame = NULL, *sw_frame = NULL;
    AVFrame *tmp_frame = NULL;
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
            goto fail;
        }

        ret = avcodec_receive_frame(avctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&frame);
            av_frame_free(&sw_frame);
            return 0;
        } else if (ret < 0) {
            fprintf(stderr, "Error while decoding\n");
            goto fail;
        }

        tmp_frame = frame;

        size = av_image_get_buffer_size(tmp_frame->format, tmp_frame->width,
                                        tmp_frame->height, 1);
        buffer = av_malloc(size);
        if (!buffer) {
            fprintf(stderr, "Can not alloc buffer\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        if (ret < 0) {
            fprintf(stderr, "Can not copy image to buffer\n");
            goto fail;
        }


        AVFrame* pFrameRGB=allocateFrame(800, 600, FORMAT);

        sws_scale(sws_ctx, (uint8_t const * const *)tmp_frame->data,
                tmp_frame->linesize, 0, tmp_frame->height,
                pFrameRGB->data, pFrameRGB->linesize);

        imageNumber += 1;

        if (imageNumber % 100 == 0) {
            snprintf(buf, sizeof(buf), "/tmp/%s_%03d.ppm", "swdecode", imageNumber);
            ppm_save(pFrameRGB->data[0], pFrameRGB->linesize[0], pFrameRGB->width, pFrameRGB->height, buf);
        }

        av_freep(&pFrameRGB->opaque);
        av_frame_free(&pFrameRGB);

    }

    fail:
        av_frame_free(&frame);
        av_frame_free(&sw_frame);
        av_freep(&buffer);
        if (ret < 0)
            return ret;
}

int main(int argc, char *argv[])
{
    AVFormatContext *input_ctx = NULL;
    int video_stream, ret;
    AVStream *video = NULL;
    AVCodecContext *decoder_ctx = NULL;
    const AVCodec *decoder = NULL;
    AVPacket packet;
    int i;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <device type> <input file>\n", argv[0]);
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

    if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
        return AVERROR(ENOMEM);

    video = input_ctx->streams[video_stream];
    if (avcodec_parameters_to_context(decoder_ctx, video->codecpar) < 0)
        return -1;

    decoder_ctx->thread_count = 0; // 0 = automatic
    decoder_ctx->thread_type = (FF_THREAD_FRAME | FF_THREAD_SLICE);

    if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream);
        return -1;
    }

    struct SwsContext* sws_ctx = sws_getContext(decoder_ctx->width,
                                decoder_ctx->height,
                                decoder_ctx->pix_fmt,
                                800,
                                600,
                                FORMAT,
                                SWS_BILINEAR,
                                NULL,
                                NULL,
                                NULL
                            );


    printf("Decoder name: %s\n", decoder->name);


   long long start = time(NULL);
   long frames = 0;

    /* actual decoding and dump the raw data */
    while (ret >= 0) {
      //  fprintf(stdout, "Frame .. \n");
        if ((ret = av_read_frame(input_ctx, &packet)) < 0)
            break;

        if (video_stream == packet.stream_index)
            ret = decode_write(decoder_ctx, &packet, sws_ctx);

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
    ret = decode_write(decoder_ctx, &packet, sws_ctx);
    av_packet_unref(&packet);



   long long end = time(NULL);
   int took = (end - start);
   fprintf(stdout, "Took %d\n", took);
   fprintf(stdout, "FPS %f\n", frames / (double)took);


    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&input_ctx);

    return 0;
}
}