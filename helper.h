#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>

static AVFrame* allocateFrame(int width, int height, AVPixelFormat format)
{
    AVFrame* pFrameRGB=av_frame_alloc();
    int numBytes=avpicture_get_size(format, width, height);

    uint8_t* buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
    avpicture_fill((AVPicture *)pFrameRGB, buffer, format,
                    width, height);
    pFrameRGB->opaque = buffer;
    pFrameRGB->width = width;
    pFrameRGB->height = height;
    return pFrameRGB;
}