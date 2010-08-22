// 
//   Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010 Free Software
//   Foundation, Inc
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

#ifndef GNASH_DUMP_H
#define GNASH_DUMP_H

#ifdef HAVE_CONFIG_H
#include "gnashconfig.h"
#endif

#include "dsodefs.h" // for DSOEXPORT
#include "gui.h" // for inheritance
#include <string>
#include <fstream>
#include <boost/scoped_array.hpp>

namespace gnash
{

class Renderer_agg_base;

typedef bool (*callback_t)(void*, int, void *data);

class DSOEXPORT DumpGui : public Gui
{
 public:
    DumpGui(unsigned long xid, float scale, bool loop, RunResources& r);
    ~DumpGui();
    void beforeRendering();
    bool createMenu() { return true; }
    bool createMenuBar() { return true; }
    bool createWindow(int width, int height);
    bool createWindow(const char* /*title*/, int width, int height,
            int /*x*/, int /*y*/)
        { return createWindow(width, height); }
    bool init(int argc, char **argv[]);
    virtual void quitUI();
    void renderBuffer() {return; }
    void render() { return; }
    void render(int /*minx*/, int /*miny*/, int /*maxx*/, int /*maxy*/)
         { render(); }
    bool run();
    void setInterval(unsigned int interval);
    void setTimeout(unsigned int timeout);
    bool setupEvents() { return true; }
    void setFullscreen() { return; }
    void setInvalidatedRegion(const SWFRect& /*bounds*/) { return; }
    void setInvalidatedRegions(const InvalidatedRanges& /*ranges*/) { return; }
    void setCursor(gnash_cursor_type /*newcursor*/) { return; }
    void setRenderHandlerSize(int width, int height);
    void unsetFullscreen() { return; }
    bool want_multiple_regions() { return true; }
    bool want_redraw() { return false; }
    void writeFrame();

private:
    
    Renderer_agg_base* _agg_renderer;

    // A buffer to hold the actual image data. A boost::scoped_array
    // is destroyed on reset and when it goes out of scope (including on
    // stack unwinding after an exception), so there is no need to delete
    // it.
    boost::scoped_array<unsigned char> _offscreenbuf;

    int _offscreenbuf_size;             /* size of window (bytes) */

    unsigned int _timeout;              /* maximum length of movie */
    unsigned int _framecount;           /* number of frames rendered */

    unsigned int _bpp;                  /* bits per pixel */
    std::string _pixelformat;              /* colorspace name (eg, "RGB24") */

    std::string _fileOutput;                 /* path to output file */
    std::ofstream _fileStream;        /* stream for output file */
    void init_dumpfile();               /* convenience method to create dump file */



};

// end of namespace gnash 
}

// end of __DUMP_H__
#endif
