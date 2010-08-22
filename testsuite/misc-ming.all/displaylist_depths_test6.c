/* 
 *   Copyright (C) 2007, 2009, 2010 Free Software Foundation, Inc.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */ 

/*
 * Zou Lunkai, zoulunkai@gmail.com
 *
 * Test "Jumping backward to the midle of a DisplayObject's lifetime after swap and swap back"
 *
 * run as ./displaylist_depths_test6
 *
 * Timeline:
 * 
 *   Frame  | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
 *  --------+---+---+---+---+---+---+---+
 *   Event  |   |PMM|   | T | T*|   | J |
 * 
 *  P = place (by PlaceObject2)
 *  T = transform matrix (by PlaceObject2)
 *  M = move to another depth (by swapDepth)
 *  J = jump
 *  * = jump target
 * 
 * Description:
 * 
 *  frame2: DisplayObject placed at depth -16381 at position (10,200);
 *          swap the DisplayObject to depth -10, and then swap it back to -16381;
 *  frame4: try to transform the DisplayObject to the right (50,200)
 *  frame5: try to transform the DisplayObject to the right (200,200)
 *  frame7: jump back to frame 5 and stop
 * 
 * Expected behaviour:
 * 
 *  After depth swapping in frame2, even if final depth is the same as the original one, static transformations in frame4 and frame5 have no effect.
 *  Before the jump we have a single instance at depth -16381 and position 10,200.
 *  After the jump we have the same instances at depth -16381, and still positioned at 10,200.
 *  A single instance has been constructed in total.
 *   
 */

#include "ming_utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <ming.h>

#define OUTPUT_VERSION 6
#define OUTPUT_FILENAME "displaylist_depths_test6.swf"

SWFDisplayItem add_static_mc(SWFMovie mo, const char* name, int depth, int x, int y, int width, int height);

SWFDisplayItem
add_static_mc(SWFMovie mo, const char* name, int depth, int x, int y, int width, int height)
{
  SWFShape sh;
  SWFMovieClip mc, mc2;
  SWFDisplayItem it;

  sh = make_fill_square (-(width/2), -(height/2), width, height, 255, 0, 0, 255, 0, 0);
  mc = newSWFMovieClip();
  SWFMovieClip_add(mc, (SWFBlock)sh);

  SWFMovieClip_nextFrame(mc);

  it = SWFMovie_add(mo, (SWFBlock)mc);
  SWFDisplayItem_setDepth(it, depth); 
  SWFDisplayItem_moveTo(it, x, y); 
  SWFDisplayItem_setName(it, name);

  return it;
}


int
main(int argc, char** argv)
{
  SWFMovie mo;
  SWFMovieClip dejagnuclip;
  int i;
  SWFDisplayItem it1;


  const char *srcdir=".";
  if ( argc>1 ) 
    srcdir=argv[1];
  else
  {
      //fprintf(stderr, "Usage: %s <mediadir>\n", argv[0]);
      //return 1;
  }

  Ming_init();
  mo = newSWFMovieWithVersion(OUTPUT_VERSION);
  SWFMovie_setDimension(mo, 800, 600);
  SWFMovie_setRate (mo, 2);

  dejagnuclip = get_dejagnu_clip((SWFBlock)get_default_font(srcdir), 10, 0, 0, 800, 600);
  SWFMovie_add(mo, (SWFBlock)dejagnuclip);
  add_actions(mo, "loopback = false;");
  SWFMovie_nextFrame(mo); 

  // Frame 2: Add a static movieclip at depth 3 with origin at 10,200, swap to depth -10 and swap back
  it1 = add_static_mc(mo, "static3", 3, 10, 200, 20, 20);
  add_actions(mo,
    "static3.myThing = 'guess';"
    "check_equals(static3.getDepth(), -16381);" 
    // swap to another depth
    "static3.swapDepths(-10);"    
    "check_equals(static3.getDepth(), -10);" 
    // swap back to the orignal depth
    "static3.swapDepths(-16381);" 
    "check_equals(static3.getDepth(), -16381);" 
    );
  SWFMovie_nextFrame(mo); 

  // Frame 3: nothing new
  SWFMovie_nextFrame(mo); 

  // Frame 4: move DisplayObject at depth 3 to position 50,200
  SWFDisplayItem_moveTo(it1, 50, 200); 
  add_actions(mo,
    // Immune to MOVE after swap, no matter if it swapped back to same depth
    "check_equals(static3._x, 10);" // after loop back we fail ! :(
    "check_equals(static3.getDepth(), -16381);" 
    );
  SWFMovie_nextFrame(mo); 

  // Frame 5: move DisplayObject at depth 3 to position 100,200
  SWFDisplayItem_moveTo(it1, 200, 200); 
  SWFMovie_nextFrame(mo); 

  // Frame 6: nothing new, just tests
  add_actions(mo,
    // Immune to MOVE after swap, no matter if it swapped back to same depth
    "check_equals(static3._x, 10);"  // after loop back we fail ! :(
    "check_equals(static3.getDepth(), -16381);" 
    );
  SWFMovie_nextFrame(mo); 

  // Frame 7: go to frame 4 
  add_actions(mo,
    " if(loopback == false) "
    " { "
    // this does not reset any thing
    "   gotoAndStop(5); "
    "   loopback = true; "
    " } "
    
    // Static3 refers to same instance
    "check_equals(static3.myThing, 'guess');" 
    // immune to MOVE after swap
    "check_equals(static3._x, 10);" 

    "check_equals(static3.getDepth(), -16381);" 
    "totals();"
    );
  SWFMovie_nextFrame(mo); 

  //Output movie
  puts("Saving " OUTPUT_FILENAME );
  SWFMovie_save(mo, OUTPUT_FILENAME);

  return 0;
}
