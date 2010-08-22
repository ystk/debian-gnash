/* 
 *   Copyright (C) 2007, 2008, 2009, 2010 Free Software Foundation, Inc.
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

#define INPUT_FILENAME "loadMovieTest.swf"

#include "MovieTester.h"
#include "MovieClip.h"
#include "DisplayObject.h"
#include "DisplayList.h"
#include "log.h"
#include "URL.h"
#include "VM.h"
#include "string_table.h"

#include "check.h"
#include <string>
#include <cassert>
#include "GnashSystemIOHeaders.h"

using namespace gnash;
using namespace std;

std::auto_ptr<MovieTester> tester;
MovieClip* root;

MovieClip*
getCoverArt()
{
	DisplayObject* coverartch = const_cast<DisplayObject*>(tester->findDisplayItemByName(*root, "coverart"));
	MovieClip* coverart = coverartch->to_movie();

	//log_debug("Coverart is %p, displaylist is:", coverart);
	//coverart->getDisplayList().dump();

	return coverart;
}

void
checkScribbling()
{
	MovieClip* coverart = getCoverArt();

	size_t initial_child_count = coverart->getDisplayList().size();

	tester->movePointerTo(73, 204); // the "Scribble" button
	check(tester->isMouseOverMouseEntity());
	for (int i=1; i<=5; ++i) {
		tester->click();
		check_equals(coverart->getDisplayList().size(), initial_child_count+i);
	}

	tester->movePointerTo(59, 225); // the "clear" button
	check(tester->isMouseOverMouseEntity());
	tester->click();
	check_equals(coverart->getDisplayList().size(), initial_child_count);
}

int
main(int /*argc*/, char** /*argv*/)
{
	gnash::LogFile& dbglogfile = gnash::LogFile::getDefaultInstance();
	dbglogfile.setVerbosity(1);

	string filename = string(TGTDIR) + string("/") + string(INPUT_FILENAME);
	tester.reset(new MovieTester(filename));

	URL baseURL(filename);
	URL mediaURL(MEDIADIR"/");
	URL lynchURL("lynch.swf", mediaURL);
	URL greenURL("green.jpg", mediaURL);
	URL offspringURL("offspring.swf", mediaURL);
	std::string url;

	gnash::RcInitFile& rc = gnash::RcInitFile::getDefaultInstance();
	rc.addLocalSandboxPath(MEDIADIR);



	root = tester->getRootMovie();
	assert(root);

	check_equals(root->get_frame_count(), 2);
	check_equals(root->get_current_frame(), 0);

	tester->advance();
	check_equals(root->get_current_frame(), 1);

	// Verify that 'coverart' exists and is empty
	DisplayObject* coverartch = const_cast<DisplayObject*>(tester->findDisplayItemByName(*root, "coverart"));
	MovieClip* coverart = coverartch->to_movie();
	check(coverart);
	url = coverart->get_root()->url();
	check_equals(coverart->get_root()->url(), baseURL.str());

	// Check scribbling on the empty canvas
	checkScribbling();

	// Click on the first (lynch)
	tester->movePointerTo(80, 80);
	check(tester->isMouseOverMouseEntity());
	tester->pressMouseButton();

    // Wait for the movie to load
    // TODO: drop this test and use a self-containment instead
    do {
	    usleep(500); // give it some time...
	    tester->advance(); // loads (should) happen on next advance
	    coverartch = const_cast<DisplayObject*>(tester->findDisplayItemByName(*root, "coverart"));
    } while (coverartch->to_movie() == coverart);

	coverart = coverartch->to_movie();
	check_equals(coverart->get_root()->url(), lynchURL.str());

	tester->depressMouseButton();

	// Check scribbling on the lynch
	checkScribbling();

	// Run 'coverart' tests..
	tester->movePointerTo(640,180);
	tester->click(); tester->advance();

	// Click on the second (green)
	tester->movePointerTo(280, 80);
	check(tester->isMouseOverMouseEntity());
	tester->click();

    // Wait for the movie to load
    // TODO: drop this test and use a self-containment instead
    do {
	    usleep(500); // give it some time... 
	    tester->advance(); // loads (should) happen on next advance
	    coverartch = const_cast<DisplayObject*>(tester->findDisplayItemByName(*root, "coverart"));
    } while (coverartch->to_movie() == coverart);

	coverart = coverartch->to_movie();
	check_equals(coverart->get_root()->url(), greenURL.str());
	// TODO: find a way to test if the jpeg is really displayed
	//       (like turn it into a mouse-event-handling char and use isMouseOverActiveEntity ?)

	// Check scribbling on the jpeg
	checkScribbling();

	// Run 'coverart' tests..
	tester->movePointerTo(640,180);
	tester->click(); tester->advance();

	// Click on the third (offspring)
	tester->movePointerTo(480, 80);
	check(tester->isMouseOverMouseEntity());
	tester->click();

    // Wait for the movie to load
    // TODO: drop this test and use a self-containment instead
    do {
	    usleep(500); // give it some time... 
	    tester->advance(); // loads (should) happen on next advance
	    coverartch = const_cast<DisplayObject*>(tester->findDisplayItemByName(*root, "coverart"));
    } while (coverartch->to_movie() == coverart);

	coverart = coverartch->to_movie();
	check_equals(coverart->get_root()->url(), offspringURL.str());

	// Check scribbling on the offspring
	checkScribbling();

	// Run 'coverart' tests..
	tester->movePointerTo(640,180);
	tester->click(); tester->advance();

	// Get summary ...
	tester->pressKey(key::SHIFT);
	tester->click(); tester->advance();
	tester->releaseKey(key::SHIFT);

	// Consistency checking
	string_table& st = getStringTable(*getObject(root));
	as_value eot;
	// It's an swf6, so lowercase 'END_OF_TEST'
	bool endOfTestFound = getObject(root)->get_member(st.find("end_of_test"), &eot);
	check(endOfTestFound);
	if ( endOfTestFound )
	{
		//cerr << eot.toDebugString() << endl;
		check_equals(eot.to_bool(), true);
	}

}

