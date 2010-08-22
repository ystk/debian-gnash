/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstversion.h: Version information for GStreamer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * NOTE: this file is derived from gst/gstversion.h. However, since no
 * nontrivial modifications were made it does not meet the originality
 * requirements for copyrightability of derivative works. The copyright
 * of this file, therefore, remains with the original authors.
 */

#ifndef __GNASH_GST_VERSION_H__
#define __GNASH_GST_VERSION_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#ifndef GST_CHECK_VERSION
#define	GST_CHECK_VERSION(major,minor,micro)	\
    (GST_VERSION_MAJOR > (major) || \
     (GST_VERSION_MAJOR == (major) && GST_VERSION_MINOR > (minor)) || \
     (GST_VERSION_MAJOR == (major) && GST_VERSION_MINOR == (minor) && \
      GST_VERSION_MICRO >= (micro)))
#endif

#if GST_CHECK_VERSION(0,10,13)
# define GNASH_GST_PLUGIN_DEFINE GST_PLUGIN_DEFINE
#else
# define GNASH_GST_PLUGIN_DEFINE GST_PLUGIN_DEFINE_STATIC
#endif

G_END_DECLS

#endif /* __GNASH_GST_VERSION_H__ */
