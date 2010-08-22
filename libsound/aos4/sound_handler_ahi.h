// sound_handler_ahi.h: Sound handling using standard AHI
//
//   Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010 Free Software
//   Foundation, Inc
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#ifndef SOUND_HANDLER_AHI_H
#define SOUND_HANDLER_AHI_H

#include "sound_handler.h" // for inheritance

#include <string>
#include <set> // for composition (InputStreams)
#include <fstream> // for composition (file_stream)
#include <boost/thread/mutex.hpp>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/ahi.h>

#include <devices/ahi.h>
#include <exec/memory.h>

#include <sys/types.h>
// Forward declarations
namespace gnash {
    class SimpleBuffer;
    namespace sound {
        class EmbedSound;
        class InputStream;
    }
}

namespace gnash {
namespace sound {

/// AHI-based sound_handler
class AOS4_sound_handler : public sound_handler
{
private:
	struct MsgPort 		*_port;
	uint32 				 _timerSig;
	struct TimeRequest 	*_timerio;
	struct TimerIFace 	*ITimer;

	struct AHIIFace 	*IAHI;
	struct Library 		*AHIBase;
	struct MsgPort		*AHImp;					//AHI Message Port
	struct AHIRequest	*AHIio;
	BYTE				 AHIDevice;
	struct AHIRequest	*AHIios[2];
	APTR				 AHIiocopy;
	ULONG				 AHICurBuf;
	bool				 AHIReqSent[2];
	UBYTE				*PlayBuffer[2];
	ULONG 				 BufferFill;
	UBYTE 				*BufferPointer;
	ULONG				 Buffer;

	struct Process 		*AudioPump;

	bool TimerInit(void);
	void TimerExit(void);
	void TimerReset(uint32 microDelay);

    /// Initialize audio card
    void initAudio();
    void openAudio();
    void closeAudio();

    bool _audioOpened;
	bool _closing;

	struct DeathMessage *_dmsg; 	// the child Death Message
	struct MsgPort *_DMreplyport;	// and its port
	
    /// Mutex for making sure threads doesn't mess things up
    boost::mutex _mutex;

    /// Mutex protecting _muted (defined in base class)
    mutable boost::mutex _mutedMutex;

    /// File stream for dump file
    //
    /// TODO: move to base class ?
    ///
    std::ofstream file_stream;

    // write a .WAV file header
    void write_wave_header(std::ofstream& outfile);

    // See dox in sound_handler.h
    void mix(boost::int16_t* outSamples, boost::int16_t* inSamples,
                unsigned int nSamples, float volume);

	void MixAudio (boost::uint8_t *dst, const boost::uint8_t *src, boost::uint32_t len, int volume);

public:

    AOS4_sound_handler(media::MediaHandler* m);

    AOS4_sound_handler(media::MediaHandler* m, const std::string& wave_file);

    ~AOS4_sound_handler();

    // See dox in sound_handler.h
    virtual int create_sound(std::auto_ptr<SimpleBuffer> data, std::auto_ptr<media::SoundInfo> sinfo);

    // See dox in sound_handler.h
    // overridden to serialize access to the data buffer slot
    virtual StreamBlockId addSoundBlock(unsigned char* data,
                                       unsigned int data_bytes,
                                       unsigned int sample_count,
                                       int streamId);

    // See dox in sound_handler.h
    virtual void    stop_sound(int sound_handle);

    // See dox in sound_handler.h
    virtual void    delete_sound(int sound_handle);

    // See dox in sound_handler.h
    virtual void reset();

    // See dox in sound_handler.h
    virtual void    stop_all_sounds();

    // See dox in sound_handler.h
    virtual int get_volume(int sound_handle);

    // See dox in sound_handler.h
    virtual void    set_volume(int sound_handle, int volume);

    // See dox in sound_handler.h
    virtual media::SoundInfo* get_sound_info(int soundHandle);

    // See dox in sound_handler.h
    // overridden to serialize access to the _muted member
    virtual void mute();

    // See dox in sound_handler.h
    // overridden to serialize access to the _muted member
    virtual void unmute();

    // See dox in sound_handler.h
    // overridden to serialize access to the _muted member
    virtual bool is_muted() const;

    // See dox in sound_handler.h
    // overridden to close audio card
    virtual void pause();

    // See dox in sound_handler.h
    // overridden to open audio card
    virtual void unpause();

    // See dox in sound_handler.h
    virtual unsigned int get_duration(int sound_handle);

    // See dox in sound_handler.h
    virtual unsigned int tell(int sound_handle);

    // See dox in sound_handler.h
    // Overridden to unpause SDL audio
    void plugInputStream(std::auto_ptr<InputStream> in);

    // See dox in sound_handler.h
    void fetchSamples(boost::int16_t* to, unsigned int nSamples);

	int audioTask();
};



} // gnash.sound namespace
} // namespace gnash

#endif // SOUND_HANDLER_AHI_H
