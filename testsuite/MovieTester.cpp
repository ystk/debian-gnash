/* 
 *   Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010 Free Software Foundation, Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *
 */ 

#include "MovieTester.h"
#include "GnashException.h"
#include "URL.h"
#include "noseek_fd_adapter.h"
#include "movie_definition.h"
#include "Movie.h"
#include "movie_root.h"
#include "MovieClip.h"
#include "MovieFactory.h"
#include "sound_handler.h" // for creating the "test" sound handlers
#include "NullSoundHandler.h"
#include "RGBA.h" // for rgba class (pixel checking)
#include "FuzzyPixel.h" // for pixel checking
#include "Renderer.h"
#include "ManualClock.h" // for use by advance
#include "StreamProvider.h" // for passing to RunResources
#include "swf/TagLoadersTable.h"
#include "swf/DefaultTagLoaders.h"

#include "MediaHandler.h"

#ifdef RENDERER_CAIRO
# include "Renderer_cairo.h"
#endif
#ifdef RENDERER_OPENGL
# include "Renderer_ogl.h"
#endif
#ifdef RENDERER_AGG
# include "Renderer_agg.h"
#endif

#include "MediaHandler.h"

#include <cstdio>
#include <string>
#include <memory> // for auto_ptr
#include <cmath> // for ceil and (possibly) exp2
#include <iostream>
#include <boost/shared_ptr.hpp>

#define SHOW_INVALIDATED_BOUNDS_ON_ADVANCE 1

#ifdef SHOW_INVALIDATED_BOUNDS_ON_ADVANCE
#include <sstream>
#endif


using std::cout;
using std::endl;

namespace gnash {

MovieTester::MovieTester(const std::string& url)
    :
    _forceRedraw(true)
{
    
    // Initialize gnash code lib
    gnashInit();
    
    // Initialize the testing media handlers
    initTestingMediaHandlers();
    
    // Initialize the sound handler(s)
    initTestingSoundHandlers();
    
    _runResources.reset(new RunResources(url));
    _runResources->setSoundHandler(_sound_handler);
    _runResources->setMediaHandler(_mediaHandler);
    
    boost::shared_ptr<SWF::TagLoadersTable> loaders(new SWF::TagLoadersTable());
    addDefaultLoaders(*loaders);
    
    _runResources->setTagLoaders(loaders);
    
    _runResources->setStreamProvider(boost::shared_ptr<StreamProvider>(
						       new StreamProvider));

    if ( url == "-" ) {
	std::auto_ptr<IOChannel> in (
		noseek_fd_adapter::make_stream(fileno(stdin))
				     );
		_movie_def = MovieFactory::makeMovie(in, url, *_runResources, false);
	} else {
	URL urlObj(url);
	if ( urlObj.protocol() == "file" ) {
	    RcInitFile& rcfile = RcInitFile::getDefaultInstance();
	    const std::string& path = urlObj.path();
#if 1 // add the *directory* the movie was loaded from to the local sandbox path
	    size_t lastSlash = path.find_last_of('/');
	    std::string dir = path.substr(0, lastSlash+1);
	    rcfile.addLocalSandboxPath(dir);
	    log_debug(_("%s appended to local sandboxes"), dir.c_str());
#else // add the *file* to be loaded to the local sandbox path
	    rcfile.addLocalSandboxPath(path);
	    log_debug(_("%s appended to local sandboxes"), path.c_str());
#endif
	}
	// _url should be always set at this point...
	_movie_def = MovieFactory::makeMovie(urlObj, *_runResources,
					     NULL, false);
    }
    
    if ( ! _movie_def )	{
	throw GnashException("Could not load movie from "+url);
    }
    
    _movie_root = new movie_root(*_movie_def, _clock, *_runResources);
    
    // Initialize viewport size with the one advertised in the header
    _width = unsigned(_movie_def->get_width_pixels());
    _height = unsigned(_movie_def->get_height_pixels());
    
    // Initialize the testing renderers
    initTestingRenderers();
    
    // Now complete load of the movie
    _movie_def->completeLoad();
    _movie_def->ensure_frame_loaded(_movie_def->get_frame_count());
    
    // Activate verbosity so that self-contained testcases are
    // also used 
    gnash::LogFile& dbglogfile = gnash::LogFile::getDefaultInstance();
    dbglogfile.setVerbosity(1);
    
    // Finally, place the root movie on the stage ...
    MovieClip::MovieVariables v;
    // We pass 'v' twice, as the second one is for scriptable
    // Variables, which isn't fully implemented yet.
    _movie_root->init(_movie_def.get(), v, v);
    
    // ... and render it
    render();
}
    
void
MovieTester::render(boost::shared_ptr<Renderer> h,
		    InvalidatedRanges& invalidated_regions) 
{
    
    // This is a bit dangerous, as there isn't really support for swapping
    // renderers during runtime; though the only problem is likely to be
    // that CachedBitmaps are missing.
    _runResources->setRenderer(h);
    
    h->set_invalidated_regions(invalidated_regions);
    
    // We call display here to simulate effect of a real run.
    //
    // What we're particularly interested about is 
    // proper computation of invalidated bounds, which
    // needs clear_invalidated() to be called.
    // display() will call clear_invalidated() on DisplayObjects
    // actually modified so we're fine with that.
    //
    // Directly calling _movie->clear_invalidated() here
    // also work currently, as invalidating the topmost
    // movie will force recomputation of all invalidated
    // bounds. Still, possible future changes might 
    // introduce differences, so better to reproduce
    // real runs as close as possible, by calling display().
    //
    _movie_root->display();
}
    
void
MovieTester::redraw()
{
    _forceRedraw=true;
    render();
}
    
void
MovieTester::render() 
{
    // Get invalidated ranges and cache them
    _invalidatedBounds.setNull();
    
    _movie_root->add_invalidated_bounds(_invalidatedBounds, false);
    
#ifdef SHOW_INVALIDATED_BOUNDS_ON_ADVANCE
    const MovieClip* r = getRootMovie();
    std::cout << "frame " << r->get_current_frame() << ") Invalidated bounds " << _invalidatedBounds << std::endl;
#endif
    
    // Force full redraw by using a WORLD invalidated ranges
    InvalidatedRanges ranges = _invalidatedBounds; 
    if ( _forceRedraw )	{
	ranges.setWorld(); // set to world if asked a full redraw
	_forceRedraw = false; // reset to no forced redraw
    }
    
    for (TestingRenderers::const_iterator it=_testingRenderers.begin(),
	     itE=_testingRenderers.end(); it != itE; ++it) {
	const TestingRenderer& rend = *it;
	render(rend.getRenderer(), ranges);
    }
    
    if ( _testingRenderers.empty() ) {
	// Make sure display is called in any case 
	//
	// What we're particularly interested about is 
	// proper computation of invalidated bounds, which
	// needs clear_invalidated() to be called.
	// display() will call clear_invalidated() on DisplayObjects
	// actually modified so we're fine with that.
	//
	// Directly calling _movie->clear_invalidated() here
	// also work currently, as invalidating the topmost
	// movie will force recomputation of all invalidated
	// bounds. Still, possible future changes might 
	// introduce differences, so better to reproduce
	// real runs as close as possible, by calling display().
	//
	_movie_root->display();
    }
}
    
void
MovieTester::advanceClock(unsigned long ms)
{
    _clock.advance(ms);
    
    if ( _sound_handler ) {
        // We need to fetch as many samples
        // as needed for a theoretical 44100hz loop.
        // That is 44100 samples each second.
        // 44100/1000 = x/ms
        //  x = (44100*ms) / 1000
        unsigned int nSamples = (441*ms) / 10;
        if ( ms%10 ) {
            log_error("MovieTester::advanceClock: %d ms lost in sound advancement",
		      ms%10);
        }
	
        // We double because sound_handler interface takes
        // "mono" samples... (eh.. would be wise to change)
        unsigned int toFetch = nSamples*2;
	
        log_debug("advanceClock(%d) needs to fetch %d samples", ms, toFetch);
	
        boost::int16_t samples[1024];
        while (toFetch) {
            unsigned int n = std::min(toFetch, 1024u);
            _sound_handler->fetchSamples((boost::int16_t*)&samples, n);
            toFetch -= n;
        }
    }
}

void
MovieTester::advance(bool updateClock)
{
    if ( updateClock ) {
	// TODO: cache 'clockAdvance' 
	float fps = _movie_def->get_frame_rate();
	unsigned long clockAdvance = long(1000/fps);
	advanceClock(clockAdvance);
    }
    
    _movie_root->advance();
    
    render();
    
}
    
void
MovieTester::resizeStage(int x, int y)
{
    _movie_root->setDimensions(x, y);
    
    if (_movie_root->getStageScaleMode() != movie_root::SCALEMODE_NOSCALE) {
	// TODO: fix to deal with all scale modes
	//       and alignments ?
	
	// set new scale value
	float xscale = x / _movie_def->get_width_pixels();
	float yscale = y / _movie_def->get_height_pixels();
	
	if (xscale < yscale) yscale = xscale;
	if (yscale < xscale) xscale = yscale;
	
        // Scale for all renderers.
        for (TestingRenderers::iterator it=_testingRenderers.begin(),
		 itE=_testingRenderers.end(); it != itE; ++it) {
            TestingRenderer& rend = *it;
            Renderer* h = rend.getRenderer().get();
            h->set_scale(xscale, yscale);
        }
    }   
}

const DisplayObject*
MovieTester::findDisplayItemByName(const MovieClip& mc,
		const std::string& name) 
{
    const DisplayList& dlist = mc.getDisplayList();
    string_table& st = getStringTable(*getObject(&mc));
    return dlist.getDisplayObjectByName(st, st.find(name), false);
}

const DisplayObject*
MovieTester::findDisplayItemByDepth(const MovieClip& mc,
		int depth)
{
    const DisplayList& dlist = mc.getDisplayList();
    return dlist.getDisplayObjectAtDepth(depth);
}

void
MovieTester::movePointerTo(int x, int y)
{
    _x = x;
    _y = y;
    if ( _movie_root->mouseMoved(x, y) ) render();
}

void
MovieTester::checkPixel(int x, int y, unsigned radius, const rgba& color,
		short unsigned tolerance, const std::string& label, bool expectFailure) const
{
    if ( ! canTestRendering() )	{
	std::stringstream ss;
	ss << "exp:" << color.toShortString() << " ";
	cout << "UNTESTED: NORENDERER: pix:" << x << "," << y << " exp:" << color.toShortString() << " " << label << endl;
    }
    
    FuzzyPixel exp(color, tolerance);
    const char* X="";
    if ( expectFailure ) X="X";
    
    //std::cout <<"chekPixel(" << color << ") called" << std::endl;
    
    for (TestingRenderers::const_iterator it=_testingRenderers.begin(),
	     itE=_testingRenderers.end(); it != itE; ++it) {
	const TestingRenderer& rend = *it;
	
	std::stringstream ss;
	ss << rend.getName() <<" ";
	ss << "pix:" << x << "," << y <<" ";
	
	rgba obt_col;
	
	const Renderer& handler = *rend.getRenderer();
	
	if ( ! handler.getAveragePixel(obt_col, x, y, radius) ) {
	    ss << " is out of rendering buffer";
	    cout << X << "FAILED: " << ss.str() << " (" << label << ")" << endl;
	    continue;
	}
	
	// Find minimum tolerance as a function of BPP
	
	unsigned short minRendererTolerance = 1;
	unsigned int bpp = handler.getBitsPerPixel();
	if ( bpp ) {
	    // UdoG: check_pixel should *always* tolerate at least 2 ^ (8 - bpp/3)
	    minRendererTolerance = int(ceil(exp2(8 - bpp/3)));
	}
	
	//unsigned short tol = std::max(tolerance, minRendererTolerance);
	unsigned short tol = tolerance*minRendererTolerance; 
	
	ss << "exp:" << color.toShortString() << " ";
	ss << "obt:" << obt_col.toShortString() << " ";
	ss << "tol:" << tol;
	
	FuzzyPixel obt(obt_col, tol);
	// equality operator would use tolerance of most tolerating FuzzyPixel
	if (exp ==  obt) {
	    cout << X << "PASSED: " << ss.str() << " (" << label << ")" << endl;
	} else {
	    cout << X << "FAILED: " << ss.str() << " (" << label << ")" << endl;
	}
    }
}
    
void
MovieTester::pressMouseButton()
{
    if ( _movie_root->mouseClick(true) ) {
	render();
    }
}

void
MovieTester::depressMouseButton()
{
    if ( _movie_root->mouseClick(false) ) {
	render();
    }
}

void
MovieTester::click()
{
    int wantRedraw = 0;
    if ( _movie_root->mouseClick(true) ) ++wantRedraw;
    if ( _movie_root->mouseClick(false) ) ++wantRedraw;
    
    if ( wantRedraw ) render();
}

void
MovieTester::scrollMouse(int delta)
{
    if (_movie_root->mouseWheel(delta)) render();
}

void
MovieTester::pressKey(key::code code)
{
    if ( _movie_root->keyEvent(code, true) ) {
	render();
    }
}

void
MovieTester::releaseKey(key::code code)
{
    if ( _movie_root->keyEvent(code, false) ) {
	render();
    }
}

bool
MovieTester::isMouseOverMouseEntity()
{
    return (_movie_root->getActiveEntityUnderPointer());
}

geometry::SnappingRanges2d<int>
MovieTester::getInvalidatedRanges() const
{
    using namespace gnash::geometry;
    
    SnappingRanges2d<float> ranges = _invalidatedBounds;
    
    // scale by 1/20 (twips to pixels)
    ranges.scale(1.0/20);
    
    // Convert to integer range.
    SnappingRanges2d<int> pixranges(ranges);
    
    return pixranges;
    
}

int
MovieTester::soundsStarted()
{
    if ( ! _sound_handler.get() ) return 0;
    return _sound_handler->numSoundsStarted();
}

int
MovieTester::soundsStopped()
{
    if ( ! _sound_handler.get() ) return 0;
    return _sound_handler->numSoundsStopped();
}

void
MovieTester::initTestingRenderers()
{
    boost::shared_ptr<Renderer> handler;
    
    // TODO: add support for testing multiple renderers
    // This is tricky as requires changes in the core lib
    
#ifdef RENDERER_AGG
    // Initialize AGG
    static const char* aggPixelFormats[] = {
	"RGB555", "RGB565", "RGBA16",
	"RGB24", "BGR24", "RGBA32", "BGRA32",
	"ARGB32", "ABGR32"
    };
    
    for (unsigned i=0; i<sizeof(aggPixelFormats)/sizeof(*aggPixelFormats); ++i)	{
	const char* pixelFormat = aggPixelFormats[i];
	std::string name = "AGG_" + std::string(pixelFormat);
	
	handler.reset( create_Renderer_agg(pixelFormat) );
	if ( handler.get() ) {
	    //log_debug("Renderer %s initialized", name.c_str());
	    std::cout << "Renderer " << name << " initialized" << std::endl;
	    addTestingRenderer(handler, name); 
	} else {
	    std::cout << "Renderer " << name << " not supported" << std::endl;
	}
    }
#endif // RENDERER_AGG
    
#ifdef RENDERER_CAIRO
    // Initialize Cairo
    handler.reset(renderer::cairo::create_handler());
    
    addTestingRenderer(handler, "Cairo");
#endif
    
#ifdef RENDERER_OPENGL
    // Initialize opengl renderer
    handler.reset(create_Renderer_ogl(false));
    addTestingRenderer(handler, "OpenGL");
#endif
}

void
MovieTester::addTestingRenderer(boost::shared_ptr<Renderer> h,
        const std::string& name)
{
    if ( ! h->initTestBuffer(_width, _height) )	{
	std::cout << "UNTESTED: render handler " << name
		  << " doesn't support in-memory rendering "
		  << std::endl;
	return;
    }
    
    // TODO: make the core lib support this
    if ( ! _testingRenderers.empty() ) {
	std::cout << "UNTESTED: can't test render handler " << name
		  << " because gnash core lib is unable to support testing of "
		  << "multiple renderers from a single process "
		  << "and we're already testing render handler "
		  << _testingRenderers.front().getName()
		  << std::endl;
	return;
    }
    
    _testingRenderers.push_back(TestingRenderer(h, name));
    
    // this will be needed till we allow run-time swapping of renderers,
    // see above UNTESTED message...
    _runResources->setRenderer(_testingRenderers.back().getRenderer());
}
    
bool
MovieTester::canTestVideo() const
{
    if ( ! canTestSound() ) return false;

    return true;
}

void
MovieTester::initTestingSoundHandlers()
{
    // Currently, SoundHandler can't be constructed
    // w/out a registered MediaHandler .
    // Should be fixed though...
    if (_mediaHandler.get()) {
        log_error("MediaHandler::get returns: %s", _mediaHandler.get());
        _sound_handler.reset(new sound::NullSoundHandler(_mediaHandler.get()));
    }
}

void
MovieTester::initTestingMediaHandlers()
{
    // TODO: allow selection.
    _mediaHandler.reset(media::MediaFactory::instance().get(""));
}

void
MovieTester::restart() 
{
    _movie_root->clear(); // restart();
    MovieClip::MovieVariables v;
    // We pass 'v' twice, as the second one is for scriptable
    // Variables, which isn't fully implemented yet.
    _movie_root->init(_movie_def.get(), v, v);
    
    // Set _movie before calling ::render
    render();
}

} // namespace gnash
