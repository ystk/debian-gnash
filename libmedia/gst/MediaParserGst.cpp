// MediaParserFfmpeg.cpp: FFMPG media parsers, for Gnash
//
//   Copyright (C) 2008, 2009, 2010 Free Software Foundation, Inc.
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


#include "MediaParserGst.h"
#include "GnashException.h"
#include "log.h"
#include "IOChannel.h"

#include "GstUtil.h" // for GST_TIME_AS_MSECONDS
#include "swfdec_codec_gst.h"
#include <iostream>
#include <fstream>

#define PUSHBUF_SIZE 1024
//#define MIN_PROBE_SIZE (PUSHBUF_SIZE * 3)
#define MIN_PROBE_SIZE 0

// #define GNASH_DEBUG_DATAFLOW

namespace gnash {
namespace media {
namespace gst {


MediaParserGst::MediaParserGst(std::auto_ptr<IOChannel> stream)
    : MediaParser(stream),
      _bin(NULL),
      _srcpad(NULL),
      _audiosink(NULL),
      _videosink(NULL),
      _demux_probe_ended(false)
{
    gst_init (NULL, NULL);

    _bin = gst_bin_new ("NULL");
    if (!_bin) {
        throw GnashException(_("MediaParserGst couldn't create a bin"));
    }

    GstElement* typefind = gst_element_factory_make("typefind", NULL);
    if (!typefind) {
        throw GnashException(_("MediaParserGst couldn't create a typefind element."));
    }
    
    gst_bin_add(GST_BIN(_bin), typefind);
    
    g_signal_connect (typefind, "have-type", G_CALLBACK (MediaParserGst::cb_typefound), this);
    
    GstCaps* srccaps = gst_caps_new_any();
    _srcpad = swfdec_gst_connect_srcpad (typefind, srccaps);
    gst_caps_unref(srccaps);

    if (!gst_element_set_state (_bin, GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS) {
        throw GnashException(_("MediaParserGst could not change element state"));
    }
    
    SimpleTimer timer;

    size_t counter = 0;
    while (!probingConditionsMet(timer)) {

        if (!pushGstBuffer()) {
            ++counter;
        }
    }

    log_debug(_("Needed %d dead iterations to detect audio type."), counter);
    
#if 0
    if (! (_videoInfo.get() || _audioInfo.get()) ) {
        throw MediaException(_("MediaParserGst failed to detect any stream types."));    
    }
#endif
    
    if (!gst_element_set_state (_bin, GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS) {
        throw MediaException(_("MediaParserGst could not change element state"));
    }

    // FIXME: threading decisions should not be up to the parser!
    startParserThread();
}

MediaParserGst::~MediaParserGst()
{
    stopParserThread();

    if (_bin) {
        gst_element_set_state (_bin, GST_STATE_NULL);
        g_object_unref (_bin);
    }  

    if (_srcpad) {
        g_object_unref (_srcpad);
    }

    if (_videosink) {
        g_object_unref (_videosink);
    }
  
    if (_audiosink) {
        g_object_unref (_audiosink);
    }

    // Sanity check for threading bug...
    assert(_enc_video_frames.empty());
    assert(_enc_audio_frames.empty());
}

bool
MediaParserGst::seek(boost::uint32_t&)
{
    LOG_ONCE(log_unimpl("MediaParserGst::seek()"))

    return false;
}


bool
MediaParserGst::parseNextChunk()
{
    boost::mutex::scoped_lock streamLock(_streamMutex);
    
    emitEncodedFrames();

    // FIXME: do we need to check this here?
    if (_stream->eof()) {
        log_debug (_("Stream EOF, emitting!"));
        _parsingComplete = true;
        return false;
    }

    pushGstBuffer();

    {
        boost::mutex::scoped_lock lock(_bytesLoadedMutex);
        _bytesLoaded = _stream->tell();
    }

    emitEncodedFrames();

    return true;

}

boost::uint64_t
MediaParserGst::getBytesLoaded() const
{
    boost::mutex::scoped_lock lock(_bytesLoadedMutex);
    return _bytesLoaded;
}

bool
MediaParserGst::pushGstBuffer()
{
    GstBuffer* buffer = gst_buffer_new_and_alloc(PUSHBUF_SIZE);

    std::streamoff ret = _stream->read(GST_BUFFER_DATA(buffer), PUSHBUF_SIZE);

    if (ret == 0) {
        if (!_stream->eof()) {
            log_error(_("MediaParserGst failed to read the stream, but did not "
                      "reach EOF!"));
        } else {
            _parsingComplete = true;
        }
        gst_buffer_unref(buffer);
        return false;
    }

    if (ret < PUSHBUF_SIZE) {
        if (!_stream->eof()) {
            log_error(_("MediaParserGst failed to read the stream, but did not "
                      "reach EOF!"));
        } else {
            _parsingComplete = true;
        }       

        GST_BUFFER_SIZE(buffer) = ret;
    }

    GstFlowReturn rv = gst_pad_push (_srcpad, buffer);
    if (!GST_FLOW_IS_SUCCESS (rv)) {
        log_error(_("MediaParserGst failed to push more data into the demuxer! "
                    "Seeking back."));
        _stream->seek(_stream->tell() - ret);
        return false;
    }
    
    return true;
}

void
MediaParserGst::emitEncodedFrames()
{
    while (!_enc_audio_frames.empty()) {
        EncodedAudioFrame* frame = _enc_audio_frames.front();
        pushEncodedAudioFrame(std::auto_ptr<EncodedAudioFrame>(frame));
       _enc_audio_frames.pop_front();
    }
    
    while (!_enc_video_frames.empty()) {
        EncodedVideoFrame* frame = _enc_video_frames.front();
        pushEncodedVideoFrame(std::auto_ptr<EncodedVideoFrame>(frame));
       _enc_video_frames.pop_front();
    }
}

void
MediaParserGst::rememberAudioFrame(EncodedAudioFrame* frame)
{
    _enc_audio_frames.push_back(frame);
}

void
MediaParserGst::rememberVideoFrame(EncodedVideoFrame* frame)
{
    _enc_video_frames.push_back(frame);
}

/// Determines whether all multimedia streams have been found.
//
///   This can happen when
///   the stream has a nondemuxable format, like MP3, or when the linked
///   demuxer has signaled "no more pads", or when the first video and
///   audio streams have been found.

bool MediaParserGst::foundAllStreams()
{
    return _demux_probe_ended || (_videoInfo.get() && _audioInfo.get());
}

/// The idea here is that probingConditionsMet will return false, unless:
/// a) all data types in the stream were found. 
/// b) The timer (currently for 1 second) has expired, if and only if we
///    succeeded in pushing MIN_PROBE_SIZE bytes into the bin. This should
///    protect low-bandwidth cases from stopping the probe early.

bool MediaParserGst::probingConditionsMet(const SimpleTimer& timer)
{
    return foundAllStreams() || (timer.expired() && getBytesLoaded() >= MIN_PROBE_SIZE);
}

static void
print_caps(GstCaps* caps)
{
    if (!caps) {
        return;
    }

    gchar* capsstr = gst_caps_to_string (caps);
    
    if (!capsstr) {
        return;
    }
    
    log_debug (_("MediaParserGst/typefound: Detected media type %s"), capsstr);

    g_free(capsstr);
}


void
MediaParserGst::link_to_fakesink(GstPad* pad)
{
    GstElement* fakesink = gst_element_factory_make("fakesink", NULL);
    
    if (!fakesink) {
        throw MediaException(_("MediaParserGst Failed to create fakesink."));
    }

    gboolean success = gst_bin_add(GST_BIN(_bin), fakesink);
    
    if (!success) {
        gst_object_unref(fakesink);
        throw MediaException(_("MediaParserGst Failed to create fakesink."));
    }
    
    GstPad* sinkpad = gst_element_get_static_pad (fakesink, "sink");
    if (!sinkpad) {
        gst_object_unref(fakesink);
        throw MediaException(_("MediaParserGst: couldn't get the fakesink "
                               "src element."));
    }
    
    GstPadLinkReturn ret = gst_pad_link(pad, sinkpad);
    if (!GST_PAD_LINK_SUCCESSFUL(ret)) {
        gst_object_unref(fakesink);
        gst_object_unref(sinkpad);
        throw MediaException(_("MediaParserGst: couln't link fakesink"));
    }
    
    if (!gst_element_set_state (_bin, GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS) {
        throw GnashException(_("MediaParserGst could not change element state"));
    }
}


// static 
void
MediaParserGst::cb_typefound (GstElement* typefind, guint /* probability */,
                              GstCaps* caps, gpointer data)
{
    print_caps(caps);

    MediaParserGst* parser = static_cast<MediaParserGst*>(data);

    GstElementFactory* demuxfactory = swfdec_gst_get_demuxer_factory (caps);
    
    if (!demuxfactory) {
    
        GstPad* srcpad = gst_element_get_static_pad (typefind, "src");
        if (!srcpad) {
            throw MediaException(_("MediaParserGst: couldn't get the typefind "
                                   "src element."));
        }

        cb_pad_added(typefind, srcpad, parser);

        gst_object_unref(GST_OBJECT(srcpad));
        
        parser->_demux_probe_ended = true;

    } else {

        GstElement* demuxer = gst_element_factory_create (demuxfactory, "demuxer");
        
        gst_object_unref(GST_OBJECT(demuxfactory));

        if (!demuxer) {
            throw MediaException(_("MediaParserGst: couldn't create the demuxer"));
        }
        
        gboolean success = gst_bin_add(GST_BIN(parser->_bin), demuxer);
        if (!success) {
            log_error(_("MediaParserGst: failed adding demuxer to bin."));
            // This error might not be fatal, so we'll continue.
        }        
        
        success = gst_element_link(typefind, demuxer);
        if (!success) {
            throw MediaException(_("MediaParserGst: failed adding demuxer to bin."));
        }
        
        g_signal_connect (demuxer, "pad-added", G_CALLBACK (MediaParserGst::cb_pad_added), parser);
        g_signal_connect (demuxer, "no-more-pads", G_CALLBACK (MediaParserGst::cb_no_more_pads), parser);
        
        if (!gst_element_set_state (parser->_bin, GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS) {
            throw GnashException(_("MediaParserGst could not change element state"));
        }
    }

}

//static 
void MediaParserGst::cb_pad_added(GstElement* /* element */, GstPad* new_pad,
                                  gpointer data)
{
    MediaParserGst* parser = static_cast<MediaParserGst*>(data);

    GstCaps* caps = gst_pad_get_caps(new_pad);    
    print_caps(caps);    
    
    GstStructure* str = gst_caps_get_structure (caps, 0);
    if (!str) {
        log_error(_("MediaParserGst: couldn't get structure name."));
        parser->link_to_fakesink(new_pad);
        return;    
    } 
    
    const gchar* caps_name = gst_structure_get_name (str);
    
    bool media_type_audio;

    if (std::equal(caps_name, caps_name+5, "audio")) {
        media_type_audio = true;
    } else if (std::equal(caps_name, caps_name+5, "video")) {
        media_type_audio = false;
    } else {
        log_error(_("MediaParserGst: ignoring stream of type %s."),
                  caps_name);
        parser->link_to_fakesink(new_pad);
        return;
    }    
    
    gboolean parsed = false;
    gboolean framed = false;
    
    gst_structure_get_boolean(str, "parsed", &parsed);
    gst_structure_get_boolean(str, "framed", &framed);
    
    bool already_parsed = parsed || framed;
    
    GstPad* final_pad = 0;
    
    if (already_parsed) {
        final_pad = new_pad;
    } else {
        // We'll try to find a parser, so that we will eventually receive
        // timestamped buffers, on which the MediaParser system relies.
        GstElementFactory* parserfactory = swfdec_gst_get_parser_factory (caps);

        if (!parserfactory) {
            log_error(_("MediaParserGst: Failed to find a parser (media: %s)."),
                      caps_name);
            parser->link_to_fakesink(new_pad);
            return;
        }

        GstElement* parserel = gst_element_factory_create (parserfactory, NULL);
        gst_object_unref (parserfactory);
        if (!parserel) {
            log_error(_("MediaParserGst: Failed to find a parser. We'll continue, "
                        "but either audio or video will not work!"));
            parser->link_to_fakesink(new_pad);
            return;
        }
                     
        gboolean success = gst_bin_add(GST_BIN(parser->_bin), parserel);
        if (!success) {
            gst_object_unref(parserel);
            log_error(_("MediaParserGst: couldn't add parser."));
            parser->link_to_fakesink(new_pad);
            return;
        }
        
        GstPad* sinkpad = gst_element_get_static_pad (parserel, "sink");
        assert(sinkpad);
        
        GstPadLinkReturn ret = gst_pad_link(new_pad, sinkpad);
        
        gst_object_unref(GST_OBJECT(sinkpad));

        if (!GST_PAD_LINK_SUCCESSFUL(ret)) {
            log_error(_("MediaParserGst: couldn't link parser."));
            parser->link_to_fakesink(new_pad);
            return;
        }
        
        final_pad = gst_element_get_static_pad (parserel, "src");
    } 

    if (media_type_audio) {
      
        parser->_audiosink = swfdec_gst_connect_sinkpad_by_pad (final_pad, caps);
        if (!parser->_audiosink) {
            log_error(_("MediaParserGst: couldn't link \"fake\" sink."));
            return;        
        }

        gst_pad_set_chain_function (parser->_audiosink, MediaParserGst::cb_chain_func_audio);
        
        g_object_set_data (G_OBJECT (parser->_audiosink), "mediaparser-obj", parser);
        
        LOG_ONCE(
        log_unimpl("MediaParserGst won't set codec, sampleRate, sampleSize, stereo and duration in AudioInfo"); 
        );
        AudioInfo* audioinfo = new AudioInfo(0, 0, 0, false, 0, CUSTOM);
        audioinfo->extra.reset(new ExtraInfoGst(caps));

        parser->_audioInfo.reset(audioinfo);
        log_debug(_("MediaParserGst: Linked audio source (type: %s)"), caps_name); 

    } else {
        parser->_videosink = swfdec_gst_connect_sinkpad_by_pad (final_pad, caps);
        if (!parser->_videosink) {
            log_error(_("MediaParserGst: couldn't link \"fake\" sink."));
            return;        
        }        
        
        gst_pad_set_chain_function (parser->_videosink, MediaParserGst::cb_chain_func_video);
        
        g_object_set_data (G_OBJECT (parser->_videosink), "mediaparser-obj", parser);

        VideoInfo* videoinfo = new VideoInfo(0, 0, 0, false, 0, CUSTOM);
        videoinfo->extra.reset(new ExtraInfoGst(caps));

        parser->_videoInfo.reset(videoinfo);
        
        log_debug(_("MediaParserGst: Linked video source (type: %s)"), caps_name); 
    }
    
    if (!already_parsed) {
        gst_object_unref(GST_OBJECT(final_pad));
    } 
    
    if (!gst_element_set_state (parser->_bin, GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS) {
        throw GnashException(_("MediaParserGst could not change element state"));
    }
}

// static
void
MediaParserGst::cb_no_more_pads (GstElement* /* demuxer */, gpointer data)
{
    MediaParserGst* parser = static_cast<MediaParserGst*>(data);

    parser->_demux_probe_ended = true;
}



// static
GstFlowReturn
MediaParserGst::cb_chain_func_video (GstPad *pad, GstBuffer *buffer)
{
    MediaParserGst* parser = (MediaParserGst*) g_object_get_data (G_OBJECT (pad), "mediaparser-obj");
    assert(parser);

    unsigned int frame_num = 0;
    unsigned int timestamp = 0;

    if (GST_BUFFER_TIMESTAMP_IS_VALID(buffer)) {
        timestamp = GST_TIME_AS_MSECONDS(GST_BUFFER_TIMESTAMP(buffer));
    }
    
    if (GST_BUFFER_OFFSET_IS_VALID(buffer)) {
        frame_num = GST_BUFFER_OFFSET(buffer);
    }

    EncodedVideoFrame* frame = new EncodedVideoFrame(NULL, GST_BUFFER_SIZE(buffer), frame_num, timestamp);

    frame->extradata.reset(new EncodedExtraGstData(buffer));
    
#ifdef GNASH_DEBUG_DATAFLOW
    log_debug("remembering video buffer with timestamp %d and frame number %d", timestamp, frame_num);
#endif

    parser->rememberVideoFrame(frame);

    return GST_FLOW_OK;
}


// static
GstFlowReturn
MediaParserGst::cb_chain_func_audio (GstPad *pad, GstBuffer *buffer)
{
    MediaParserGst* parser = (MediaParserGst*) g_object_get_data (G_OBJECT (pad), "mediaparser-obj");
    assert(parser);

    EncodedAudioFrame* frame = new EncodedAudioFrame;

    // 'dataSize' should reflect size of 'data'.
    // Since we're not allocating any 'data' there's no point
    // in setting dataSize.
    //frame->dataSize = GST_BUFFER_SIZE(buffer);

    if (GST_BUFFER_TIMESTAMP_IS_VALID(buffer)) {
        frame->timestamp = GST_TIME_AS_MSECONDS(GST_BUFFER_TIMESTAMP(buffer));
    } else {
        // What if a frame with invalid timestamp is found
        // in the middle of the stream ? Using 0 here, might
        // mean that the computed "bufferTime" is huge (0 to last valid timestamp)
        // Not necessarely a big deal, but a conceptual glitch.
        frame->timestamp = 0;
    }
    
    frame->extradata.reset(new EncodedExtraGstData(buffer));
    
#ifdef GNASH_DEBUG_DATAFLOW
    log_debug("remembering audio buffer with timestamp %d.", frame->timestamp);
#endif

    parser->rememberAudioFrame(frame);


    return GST_FLOW_OK;
}

} // gnash.media.gst namespace
} // namespace media
} // namespace gnash
