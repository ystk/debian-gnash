// GnashImageJpeg.h:  Jpeg reader, for Gnash.
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
//
// Original version by Thatcher Ulrich <tu@tulrich.com> 2002
//

#ifndef GNASH_IMAGE_JPEG_H
#define GNASH_IMAGE_JPEG_H

#include "dsodefs.h"
#include <csetjmp> // for jmp_buf
#include "GnashImage.h"


/// \brief A namespace solely for avoiding name 
/// conflicts with other external headers.
namespace jpeg {

// jpeglib.h redefines HAVE_STDLIB_H. This silences
// the warnings, but it's not good.
#undef HAVE_STDLIB_H
extern "C" {
#include <jpeglib.h>
}
#undef HAVE_STDLIB_H

}

// Forward declarations
namespace gnash { class IOChannel; }

namespace gnash
{

/// Class for reading JPEG image data. 
//
/// This uses the IJG jpeglib to implement the ImageInput interface.
class JpegImageInput : public ImageInput
{

private:

    const char* _errorOccurred;

    std::jmp_buf _jmpBuf;

    // State needed for input.
    jpeg::jpeg_decompress_struct m_cinfo;
    jpeg::jpeg_error_mgr m_jerr;

    bool _compressorOpened;

public:

    /// Construct a JpegImageInput object to read from an IOChannel.
    //
    /// @param in   The stream to read JPEG data from. Ownership is shared
    ///             between caller and JpegImageInput, so it is freed
    ///             automatically when the last owner is destroyed.
    DSOEXPORT JpegImageInput(boost::shared_ptr<IOChannel> in);

    /// Read the JPEG header information only.
    //
    /// @param maxHeaderBytes   The maximum number of bytes to read before
    ///                         Stopping. If the header is shorter, we stop
    ///                         early.
    void DSOEXPORT readHeader(unsigned int maxHeaderBytes);

    ~JpegImageInput();

    /// Begin processing the image data.
    void read();

    /// Discard any data sitting in our input buffer.
    //
    /// Use this before/after reading headers or partial image
    /// data, to avoid screwing up future reads.
    DSOEXPORT void discardPartialBuffer();

    /// Complete processing the image and clean up.
    //
    /// This should close / free all resources from libjpeg.
    void finishImage();

    /// Get the image's height in pixels.
    //
    /// @return     The height of the image in pixels.
    size_t getHeight() const;

    /// Get the image's width in pixels.
    //
    /// @return     The width of the image in pixels.
    size_t getWidth() const;

    /// Get number of components (channels)
    //
    /// @return     The number of components, e.g. 3 for RGB
    size_t getComponents() const;

    /// Read a scanline's worth of image data into the given buffer.
    //
    /// The amount of data read is getWidth() * getComponents().
    ///
    /// @param rgbData  The buffer for writing raw RGB data to.
    void readScanline(unsigned char* rgbData);

    /// Create a JpegImageInput and transfer ownership to the caller.
    //
    /// @param in   The IOChannel to read JPEG data from.
    static std::auto_ptr<ImageInput> create(boost::shared_ptr<IOChannel> in)
    {
        std::auto_ptr<ImageInput> ret(new JpegImageInput(in));
        // might throw an exception (I guess)
        if (ret.get()) ret->read();
        return ret;
    }

    /// \brief
    /// For reading SWF JPEG2-style image data, using pre-loaded
    /// headers stored in the given jpeg::input object.
    //
    /// @param loader   The JpegImageInput object to use for reading the
    ///                 data. This should have been constructed with
    ///                 createSWFJpeg2HeaderOnly().
    DSOEXPORT static std::auto_ptr<GnashImage> readSWFJpeg2WithTables(
            JpegImageInput& loader);

    /// Create a JPEG 'loader' object by reading a JPEG header.
    //
    /// This is for reusing the header information for different JPEGs images.
    //
    /// @param in               The channel to read JPEG header data from.
    /// @param maxHeaderBytes   The maximum number of bytes to read.
    static std::auto_ptr<JpegImageInput> createSWFJpeg2HeaderOnly(
            boost::shared_ptr<IOChannel> in, unsigned int maxHeaderBytes)
    {
        std::auto_ptr<JpegImageInput> ret (new JpegImageInput(in));
        // might throw an exception
        if (ret.get()) ret->readHeader(maxHeaderBytes);
        return ret;
    }

    /// This function is called when libjpeg encounters an error.
    //
    /// It is needed to avoid memory corruption during stack unwinding by
    /// freeing libjpeg resources correctly before throwing an exception.
    ///
    /// @param msg  An error message for logging.
    void errorOccurred(const char* msg);


};

// Class for writing JPEG image data.
class JpegImageOutput : public ImageOutput
{

public:

    /// Constract a JpegImageOutput for writing to an IOChannel
    //
    /// @param out      The gnash::IOChannel to write the image to
    /// @param width    The width of the resulting image
    /// @param height   The height of the resulting image.
    /// @param quality  The quality of the created image, from 1-100.
    JpegImageOutput(boost::shared_ptr<IOChannel> out, size_t width,
            size_t height, int quality);
    
    ~JpegImageOutput();

    /// Write RGB image data using the parameters supplied at construction.
    //
    /// @param rgbData  The raw RGB image data to write as a JPEG.
    void writeImageRGB(const unsigned char* rgbData);

    /// Create a JpegImageOutput, transferring ownership to the caller.
    //
    /// @param out      The gnash::IOChannel to write the image to
    /// @param width    The width of the resulting image
    /// @param height   The height of the resulting image.
    /// @param quality  The quality of the created image, from 1-100.
    static std::auto_ptr<ImageOutput> create(boost::shared_ptr<IOChannel> out,
            size_t width, size_t height, int quality);
    
private:

    jpeg::jpeg_compress_struct m_cinfo;
    jpeg::jpeg_error_mgr m_jerr;
    
};

} // namespace gnash


#endif // JPEG_H

// Local Variables:
// mode: C++
// c-basic-offset: 8 
// tab-width: 8
// indent-tabs-mode: t
// End:
