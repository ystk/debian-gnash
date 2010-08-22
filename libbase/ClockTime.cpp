// Time.cpp: clock and local time functions for Gnash
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

/// \page wall_clock_time Wall clock time
///
/// Gnash has three time implementations: one using boost::date_time,
/// which handles portability itself, one for POSIX systems and one for 
/// Win32.
///
/// Namespace clocktime contains a unified source for wall clock time: this
/// is used mainly for the timing of movie advances and in the ActionScript
/// Date class. FPS profiling also uses clocktime:: for a relatively high
/// resolution, robust timer.
///
/// The boost::date_time has the great advantage of handling portability itself,
/// as well as being able to handle a much larger range of true dates. Its
/// disadvantage is that date_time requires not only header files, but also
/// a run-time library, and thus increases the requirements.
///
/// @todo review this page, some bits seem obsoleted

#include <boost/cstdint.hpp>
#include "ClockTime.h"
#include "log.h"

// Define USE_BOOST_DATE_TIME to use boost as the basis for all
// clock time functions. The function getTimeZoneOffset() is not
// yet implemented for boost, but will only affect the Date class.
#undef USE_BOOST_DATE_TIME

#ifdef USE_BOOST_DATE_TIME

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/microsec_time_clock.hpp>

using namespace boost::posix_time;

boost::uint64_t
clocktime::getTicks()
{

    // Midnight, 1st January 1970: the Epoch.
    static const posix_time::ptime epoch (from_time_t(0));
    
    // Time between now and the Epoch.
    posix_time::time_duration elapsed = (microsec_clock::local_time() - epoch);
    
    // Divisor to convert ticks to milliseconds
    const int denominator = time_duration::ticks_per_second() / 1000.0;
    
    return elapsed.ticks() / denominator;
}

boost::int32_t
clocktime::getTimeZoneOffset()
{
    // Obviously this doesn't work yet. Using this method
    // may come up against the problem that boost won't handle
    // dates outside its limits. However, ActionScript seems
    // not to regard dates later than 2037 as having dst (this
    // may depend on a machine-specific tz database) and there
    // could also be a lower limit.

    return 0;
}

#else // not using boost::date_time

#include <ctime> // for time_t, localtime

#if !defined(HAVE_GETTIMEOFDAY) || (!defined(HAVE_TM_GMTOFF) && !defined(HAVE_TZSET))
#ifdef HAVE_FTIME
extern "C" {
#  include <sys/types.h>    // for ftime()
#  include <sys/timeb.h>    // for ftime()
}
#endif
#endif

#if !defined(HAVE_TM_GMTOFF)
# ifdef HAVE_LONG_TIMEZONE
extern long timezone;   // for tzset()/long timezone;
# endif
#endif

/// Win32 implementation for getTicks
# if defined(_WIN32) || defined(WIN32)
#  include <windows.h>
#  include <mmsystem.h>


boost::uint64_t
clocktime::getTicks()
{
    // This needs to return milliseconds. Does it?
    return timeGetTime();
}

# else // not _WIN32
#  include <sys/time.h>

boost::uint64_t
clocktime::getTicks()
{

    struct timeval tv;
    
    gettimeofday(&tv, 0);

    boost::uint64_t result = static_cast<boost::uint64_t>(tv.tv_sec) * 1000000L;

    // Time Unit: microsecond
    result += tv.tv_usec;

    return static_cast<boost::uint64_t>(result / 1000.0);
}

# endif // not WIN32

/// Common non-boost function to return the present time offset.
/// This all seems like a terrible hack. It was moved from Date.cpp,
/// whence the following explanation also comes.
///
/// If the real mktime() sees isdst == 0 with a DST date, it sets
/// t_isdst and modifies the hour fields, but we need to set the
/// specified hour in the localtime in force at that time.
///
/// To do this we set tm_isdst to the correct value for that moment in time
/// by doing an initial conversion of the time to find out is_dst for that
/// moment without DST, then do the real conversion.
/// This may still get things wrong around the hour when the clocks go back
///
/// It also gets things wrong for very high or low time values, when the
/// localtime implementation fills the gmtoff element with 53 minutes (on
/// at least one machine, anyway).
boost::int32_t
clocktime::getTimeZoneOffset(double time)
{
    
    time_t tt = static_cast<time_t>(time / 1000.0);

    struct tm tm;

#ifdef HAVE_LOCALTIME_R

    // If the requested time exceeds the limits we return 0; otherwise we'll
    // be using uninitialized values
    if (!localtime_r(&tt, &tm)) {
        return 0;
    }
#else
    struct tm *tmp = NULL;
    tmp = localtime(&tt);
    if (!tmp) return 0; // We failed.
    memcpy(&tm, tmp, sizeof(struct tm));
#endif

    struct tm tm2 = tm;
    tm2.tm_isdst = 0;

    time_t ttmp = 0;
    
    ttmp = mktime(&tm2);

#ifdef HAVE_LOCALTIME_R
    // find out whether DST is in force
    if (!localtime_r(&ttmp, &tm2)) {
        return 0;
    }  
#else
    struct tm *tmp2 = NULL;
    tmp2 = localtime(&ttmp);
    if (!tmp2) return 0; // We failed.
    memcpy(&tm2, tmp2, sizeof(struct tm));
#endif

    // If mktime or localtime fail, tm2.tm_isdst should be unchanged,
    // so 0. That's why we don't make any checks on their success.

    tm.tm_isdst = tm2.tm_isdst;

#ifdef HAVE_TM_GMTOFF

    int offset;

    // tm_gmtoff is in seconds east of GMT; convert to minutes.
    offset = tm.tm_gmtoff / 60;
    //gnash::log_debug("Using tm.tm_gmtoff. Offset is %d", offset);
    return offset;

#else

  // Find the geographical system timezone offset and add an hour if
  // DST applies to the date.
  // To get it really right I guess we should call both gmtime()
  // and localtime() and look at the difference.
  //
  // The range of standard time is GMT-11 to GMT+14.
  // The most extreme with DST is Chatham Island GMT+12:45 +1DST

  int offset;

# if defined(HAVE_TZSET) && defined(HAVE_LONG_TIMEZONE)

    tzset();
    // timezone is seconds west of GMT
    offset = -timezone / 60;
    //gnash::log_debug("Using tzset. Offset is %d", offset);

# elif defined(HAVE_GETTIMEOFDAY)

    // gettimeofday(3):
    // "The use of the timezone structure is obsolete; the tz argument
    // should normally be specified as NULL. The tz_dsttime field has
    // never been used under Linux; it has not been and will not be
    // supported by libc or glibc."
    //
    // In practice this appears to return the present time offset including dst,
    // so adding the dst of the time specified (we do this a couple of lines
    // down) gives the correct result when it's not presently dst, the wrong
    // one when it is.
    struct timeval tv;
    struct timezone tz;
    gettimeofday(&tv, &tz);
    offset = -tz.tz_minuteswest;
    //gnash::log_debug("Using gettimeofday. Offset is %d", offset);

# elif defined(HAVE_FTIME)
    // ftime(3): "These days the contents of the timezone and dstflag
    // fields are undefined."
    // In practice, timezone is -120 in Italy when it should be -60.
    // The problem here as for gettimeofday: the offset also includes dst.
    struct timeb tb;
    
    ftime(&tb);

    // tb.timezone is number of minutes west of GMT
    offset = -tb.timezone;

# else

    offset = 0; // No idea.

# endif

  // Adjust by one hour if DST was in force at that time.
  //
  // According to http://www.timeanddate.com/time/, the only place that
  // uses DST != +1 hour is Lord Howe Island with half an hour. Tough.

    if (tm.tm_isdst == 0) {
        // DST exists and is not in effect
    }
    else if (tm.tm_isdst > 0) {
        // DST exists and was in effect
        offset += 60;
    }
    else {
        // tm_isdst is negative: cannot get TZ info.
        // Convert and print in UTC instead.
        LOG_ONCE(
            gnash::log_error(_("Cannot get requested timezone information"));
        );
        offset = 0;
    }

    return offset;

#endif // No gmoff
}



#endif // Not using boost::date_time
