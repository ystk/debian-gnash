// VideoDecoderFfmpeg.cpp: Video decoding using the FFMPEG library.
// 
//     Copyright (C) 2007, 2008, 2009, 2010 Free Software Foundation, Inc.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//

#ifdef HAVE_CONFIG_H
#include "gnashconfig.h"
#endif

#include "ffmpegHeaders.h"
#include "VideoDecoderFfmpeg.h"
#include "MediaParserFfmpeg.h" // for ExtraVideoInfoFfmpeg 
#include "GnashException.h" // for MediaException
#include "utility.h"

#include <boost/scoped_array.hpp>
#include <boost/format.hpp>
#include <algorithm>

#include "FLVParser.h"

#ifdef HAVE_VA_VA_H
#  include "vaapi_utils.h"
#  include "VideoDecoderFfmpegVaapi.h"
#  include "GnashVaapiImage.h"
#endif

namespace gnash {
namespace media {
namespace ffmpeg {
 
class VaapiContextFfmpeg;

// Forward declarations of VAAPI functions.
namespace {


    VaapiContextFfmpeg* get_vaapi_context(AVCodecContext* avctx);
    void set_vaapi_context(AVCodecContext* avctx, VaapiContextFfmpeg* vactx);
    void clear_vaapi_context(AVCodecContext* avctx);
    void reset_context(AVCodecContext* avctx, VaapiContextFfmpeg* vactx = 0);
    PixelFormat get_format(AVCodecContext* avctx, const PixelFormat* fmt);
    int get_buffer(AVCodecContext* avctx, AVFrame* pic);
    int reget_buffer(AVCodecContext* avctx, AVFrame* pic);
    void release_buffer(AVCodecContext *avctx, AVFrame *pic);
}

#ifdef HAVE_SWSCALE_H
/// A wrapper round an SwsContext that ensures it's
/// freed on destruction.
class SwsContextWrapper
{
public:

    SwsContextWrapper(SwsContext* context)
        :
        _context(context)
    {}

    ~SwsContextWrapper()
    {
         sws_freeContext(_context);
    }
    
    SwsContext* getContext() const { return _context; }

private:
    SwsContext* _context;

};
#endif


// A Wrapper ensuring an AVCodecContext is closed and freed
// on destruction.
class CodecContextWrapper
{
public:
    CodecContextWrapper(AVCodecContext* context)
        :
        _codecCtx(context)
    {}

    ~CodecContextWrapper()
    {
        if (_codecCtx)
        {
            avcodec_close(_codecCtx);
            clear_vaapi_context(_codecCtx);
            av_free(_codecCtx);
        }
    }

    AVCodecContext* getContext() const { return _codecCtx; }

private:
    AVCodecContext* _codecCtx;
};


VideoDecoderFfmpeg::VideoDecoderFfmpeg(videoCodecType format, int width, int height)
    :
    _videoCodec(NULL)
{

    CodecID codec_id = flashToFfmpegCodec(format);
    init(codec_id, width, height);

}

VideoDecoderFfmpeg::VideoDecoderFfmpeg(const VideoInfo& info)
    :
    _videoCodec(NULL)
{

    CodecID codec_id = CODEC_ID_NONE;

    if ( info.type == FLASH )
    {
        codec_id = flashToFfmpegCodec(static_cast<videoCodecType>(info.codec));
    }
    else codec_id = static_cast<CodecID>(info.codec);

    // This would cause nasty segfaults.
    if (codec_id == CODEC_ID_NONE)
    {
        boost::format msg = boost::format(_("Cannot find suitable "
                "decoder for flash codec %d")) % info.codec;
        throw MediaException(msg.str());
    }

    boost::uint8_t* extradata=0;
    int extradataSize=0;
    if (info.extra.get())
    {
        if (dynamic_cast<ExtraVideoInfoFfmpeg*>(info.extra.get())) {
            const ExtraVideoInfoFfmpeg& ei = 
                static_cast<ExtraVideoInfoFfmpeg&>(*info.extra);
            extradata = ei.data;
            extradataSize = ei.dataSize;
        }
        else if (dynamic_cast<ExtraVideoInfoFlv*>(info.extra.get())) {
            const ExtraVideoInfoFlv& ei = 
                static_cast<ExtraVideoInfoFlv&>(*info.extra);
            extradata = ei.data.get();
            extradataSize = ei.size;
        }
        else {
            std::abort();
        }
    }
    init(codec_id, info.width, info.height, extradata, extradataSize);
}

void
VideoDecoderFfmpeg::init(enum CodecID codecId, int /*width*/, int /*height*/,
        boost::uint8_t* extradata, int extradataSize)
{
    // Init the avdecoder-decoder
    avcodec_init();
    avcodec_register_all();// change this to only register need codec?

    _videoCodec = avcodec_find_decoder(codecId); 

    if (!_videoCodec) {
        throw MediaException(_("libavcodec can't decode this video format"));
    }

    _videoCodecCtx.reset(new CodecContextWrapper(avcodec_alloc_context()));
    if (!_videoCodecCtx->getContext()) {
        throw MediaException(_("libavcodec couldn't allocate context"));
    }

    AVCodecContext* const ctx = _videoCodecCtx->getContext();

    ctx->extradata = extradata;
    ctx->extradata_size = extradataSize;

    ctx->get_format     = get_format;
    ctx->get_buffer     = get_buffer;
    ctx->reget_buffer   = reget_buffer;
    ctx->release_buffer = release_buffer;

#ifdef HAVE_VA_VA_H
    if (vaapi_is_enabled()) {
        VaapiContextFfmpeg *vactx = VaapiContextFfmpeg::create(codecId);
        if (vactx)
            reset_context(ctx, vactx);
    }
#endif

    int ret = avcodec_open(ctx, _videoCodec);
    if (ret < 0) {
        boost::format msg = boost::format(_("libavcodec"
                            "failed to initialize FFMPEG "
                            "codec %s (%d)")) % 
                            _videoCodec->name % (int)codecId;

        throw MediaException(msg.str());
    }
    
    log_debug(_("VideoDecoder: initialized FFMPEG codec %s (%d)"), 
                _videoCodec->name, (int)codecId);

}

VideoDecoderFfmpeg::~VideoDecoderFfmpeg()
{
}

int
VideoDecoderFfmpeg::width() const
{
    if (!_videoCodecCtx.get()) return 0;
    return _videoCodecCtx->getContext()->width;
}

int
VideoDecoderFfmpeg::height() const
{
    if (!_videoCodecCtx.get()) return 0;
    return _videoCodecCtx->getContext()->height;
}

std::auto_ptr<GnashImage>
VideoDecoderFfmpeg::frameToImage(AVCodecContext* srcCtx,
                                 const AVFrame& srcFrameRef)
{
    const AVFrame *srcFrame = &srcFrameRef;
    PixelFormat srcPixFmt = srcCtx->pix_fmt;

    const int width = srcCtx->width;
    const int height = srcCtx->height;

#ifdef FFMPEG_VP6A
    PixelFormat pixFmt = (srcCtx->codec->id == CODEC_ID_VP6A) ?
        PIX_FMT_RGBA : PIX_FMT_RGB24;
#else 
    PixelFormat pixFmt = PIX_FMT_RGB24;
#endif 

    std::auto_ptr<GnashImage> im;

#ifdef HAVE_VA_VA_H
    VaapiContextFfmpeg * const vactx = get_vaapi_context(srcCtx);
    if (vactx) {
        VaapiSurfaceFfmpeg * const vaSurface = vaapi_get_surface(&srcFrameRef);
        if (!vaSurface) {
            im.reset();
            return im;
        }
        im.reset(new GnashVaapiImage(vaSurface->get(), GNASH_IMAGE_RGBA));
        return im;
    }
#endif

#ifdef HAVE_SWSCALE_H
    // Check whether the context wrapper exists
    // already.
    if (!_swsContext.get()) {

        _swsContext.reset(new SwsContextWrapper(
            sws_getContext(width, height, srcPixFmt, width, height,
                pixFmt, SWS_BILINEAR, NULL, NULL, NULL)
        ));
        
        // Check that the context was assigned.
        if (!_swsContext->getContext()) {

            // This means we will try to assign the 
            // context again next time.
            _swsContext.reset();
            
            // Can't do anything now, though.
            return im;
        }
    }
#endif

    int bufsize = avpicture_get_size(pixFmt, width, height);
    if (bufsize == -1) return im;

    switch (pixFmt)
    {
// As of libavcodec 0.cvs20060823-8  PIX_FMT_RGBA is unknown symbol
// As of libavcodec 0.svn20080206-17 it is a define
#ifdef PIX_FMT_RGBA
        case PIX_FMT_RGBA:
            im.reset(new ImageRGBA(width, height));
            break;
#endif
        case PIX_FMT_RGB24:
            im.reset(new ImageRGB(width, height));
            break;
        default:
            log_error("Pixel format not handled");
            return im;
    }

    AVPicture picture;

    // Let ffmpeg write directly to the GnashImage data. It is an uninitialized
    // buffer here, so do not return the image if there is any error in
    // conversion.
    avpicture_fill(&picture, im->begin(), pixFmt, width, height);

#ifndef HAVE_SWSCALE_H
    img_convert(&picture, PIX_FMT_RGB24, (AVPicture*)srcFrame,
            srcPixFmt, width, height);
#else

    // Is it possible for the context to be reset
    // to NULL once it's been created?
    assert(_swsContext->getContext());

    int rv = sws_scale(_swsContext->getContext(), 
            const_cast<uint8_t**>(srcFrame->data),
            const_cast<int*>(srcFrame->linesize), 0, height, picture.data,
            picture.linesize);

    if (rv == -1) {
        im.reset();
        return im;
    }

#endif

    return im;

}

std::auto_ptr<GnashImage>
VideoDecoderFfmpeg::decode(const boost::uint8_t* input,
        boost::uint32_t input_size)
{
    // This object shouldn't exist if there's no codec, as it can'
    // do anything anyway.
    assert(_videoCodecCtx.get());

    std::auto_ptr<GnashImage> ret;

    AVFrame* frame = avcodec_alloc_frame();
    if ( ! frame ) {
        log_error(_("Out of memory while allocating avcodec frame"));
        return ret;
    }

    int bytes = 0;    
    // no idea why avcodec_decode_video wants a non-const input...
    avcodec_decode_video(_videoCodecCtx->getContext(), frame, &bytes,
            const_cast<boost::uint8_t*>(input), input_size);
    
    if (!bytes) {
        log_error("Decoding of a video frame failed");
        av_free(frame);
        return ret;
    }

    ret = frameToImage(_videoCodecCtx->getContext(), *frame);

    // FIXME: av_free doesn't free frame->data!
    av_free(frame);
    return ret;
}


void
VideoDecoderFfmpeg::push(const EncodedVideoFrame& buffer)
{
    _video_frames.push_back(&buffer);
}

std::auto_ptr<GnashImage>
VideoDecoderFfmpeg::pop()
{
    std::auto_ptr<GnashImage> ret;

    for (std::vector<const EncodedVideoFrame*>::iterator it =
             _video_frames.begin(), end = _video_frames.end(); it != end; ++it) {
         ret = decode((*it)->data(), (*it)->dataSize());
    }

    _video_frames.clear();

    return ret;
}
    
bool
VideoDecoderFfmpeg::peek()
{
    return (!_video_frames.empty());
}

/* public static */
enum CodecID
VideoDecoderFfmpeg::flashToFfmpegCodec(videoCodecType format)
{
        // Find the decoder and init the parser
        switch(format) {
                case VIDEO_CODEC_H264:
                         return CODEC_ID_H264;
                case VIDEO_CODEC_H263:
			 // CODEC_ID_H263I didn't work with Lavc51.50.0
			 // and NetStream-SquareTest.swf
                         return CODEC_ID_FLV1;
#ifdef FFMPEG_VP6
                case VIDEO_CODEC_VP6:
                        return CODEC_ID_VP6F;
#endif
#ifdef FFMPEG_VP6A
                case VIDEO_CODEC_VP6A:
	                return CODEC_ID_VP6A;
#endif
                case VIDEO_CODEC_SCREENVIDEO:
                        return CODEC_ID_FLASHSV;
                default:
                        log_error(_("Unsupported video codec %d"),
                                static_cast<int>(format));
                        return CODEC_ID_NONE;
        }
}

namespace {

inline VaapiContextFfmpeg*
get_vaapi_context(AVCodecContext* avctx)
{
#ifdef HAVE_VA_VA_H
    return static_cast<VaapiContextFfmpeg *>(avctx->hwaccel_context);
#else
    UNUSED(avctx);
	return 0;
#endif
}

inline void
set_vaapi_context(AVCodecContext* avctx, VaapiContextFfmpeg* vactx)
{
#ifdef HAVE_VA_VA_H
    avctx->hwaccel_context = vactx;
#else
    UNUSED(avctx), UNUSED(vactx);
#endif
    
}

inline void
clear_vaapi_context(AVCodecContext* avctx)
{
#ifdef HAVE_VA_VA_H
    VaapiContextFfmpeg* const vactx = get_vaapi_context(avctx);
    if (!vactx) return;

    delete vactx;
    set_vaapi_context(avctx, NULL);
#else
    UNUSED(avctx);
#endif
}

/// (Re)set AVCodecContext to sane values 
void
reset_context(AVCodecContext* avctx, VaapiContextFfmpeg* vactx)
{
    clear_vaapi_context(avctx);
    set_vaapi_context(avctx, vactx);

    avctx->thread_count = 1;
    avctx->draw_horiz_band = 0;
    if (vactx) {
        avctx->slice_flags = SLICE_FLAG_CODED_ORDER|SLICE_FLAG_ALLOW_FIELD;
    }
    else avctx->slice_flags = 0;
}

/// AVCodecContext.get_format() implementation
PixelFormat
get_format(AVCodecContext* avctx, const PixelFormat* fmt)
{
#ifdef HAVE_VA_VA_H
    VaapiContextFfmpeg* const vactx = get_vaapi_context(avctx);

    if (vactx) {
        for (int i = 0; fmt[i] != PIX_FMT_NONE; i++) {
            if (fmt[i] != PIX_FMT_VAAPI_VLD) continue;

            if (vactx->initDecoder(avctx->width, avctx->height)) {
                return fmt[i];
            }
        }
    }
#endif

    reset_context(avctx);
    return avcodec_default_get_format(avctx, fmt);
}

/// AVCodecContext.get_buffer() implementation
int
get_buffer(AVCodecContext* avctx, AVFrame* pic)
{
    VaapiContextFfmpeg* const vactx = get_vaapi_context(avctx);
    if (!vactx) return avcodec_default_get_buffer(avctx, pic);

#ifdef HAVE_VA_VA_H
    if (!vactx->initDecoder(avctx->width, avctx->height)) return -1;

    VaapiSurfaceFfmpeg * const surface = vactx->getSurface();
    if (!surface) return -1;

    vaapi_set_surface(pic, surface);

    static unsigned int pic_num = 0;
    pic->type = FF_BUFFER_TYPE_USER;
    pic->age  = ++pic_num - surface->getPicNum();
    surface->setPicNum(pic_num);
    return 0;
#endif
    return -1;
}

/// AVCodecContext.reget_buffer() implementation
int
reget_buffer(AVCodecContext* avctx, AVFrame* pic)
{
    VaapiContextFfmpeg* const vactx = get_vaapi_context(avctx);

    if (!vactx) return avcodec_default_reget_buffer(avctx, pic);

    return get_buffer(avctx, pic);
}

/// AVCodecContext.release_buffer() implementation
void
release_buffer(AVCodecContext *avctx, AVFrame *pic)
{
    VaapiContextFfmpeg* const vactx = get_vaapi_context(avctx);
    if (!vactx) {
        avcodec_default_release_buffer(avctx, pic);
        return;
    }

#ifdef HAVE_VA_VA_H
    VaapiSurfaceFfmpeg* const surface = vaapi_get_surface(pic);
    delete surface;

    pic->data[0] = NULL;
    pic->data[1] = NULL;
    pic->data[2] = NULL;
    pic->data[3] = NULL;
#endif
}

}

} // gnash.media.ffmpeg namespace 
} // gnash.media namespace 
} // gnash namespace
