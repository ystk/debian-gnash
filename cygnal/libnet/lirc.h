// 
//   Copyright (C) 2007, 2008, 2009, 2010 Free Software Foundation, Inc.
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

#ifndef GNASH_LIRC_H
#define GNASH_LIRC_H

#include "GnashKey.h"
#include "network.h"

namespace gnash {

class DSOEXPORT Lirc : public Network {
public:
    Lirc();
    ~Lirc();
    bool init();
    bool init(const char *sockpath);
    
    // Whenever lircd receives a IR signal it will broadcast the
    // following string to each client:
    // <code> <repeat count> <button name> <remote control name>
    gnash::key::code getKey();
    const char *getButton();
  private:
    const char *_sockname;
    char *_button;
};

} // end of gnash namespace

// GNASH_LIRC_H
#endif

// Local Variables:
// mode: C++
// indent-tabs-mode: t
// End:
