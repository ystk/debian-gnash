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

#ifdef HAVE_CONFIG_H
#include "gnashconfig.h"
#endif

#include <sys/types.h>
#if !defined(HAVE_WINSOCK_H) && !defined(__riscos__) && !defined(__OS2__) && !defined(HAIKU_HOST) && !defined(_ANDROID)
# include <sys/shm.h>
# include <sys/sem.h>
# include <sys/ipc.h>
#elif !defined(__riscos__) && !defined(__OS2__) && !defined(_ANDROID)
# include <windows.h>
# include <process.h>
# include <io.h>
#endif
#include <string>
#include <vector>
#include <cerrno>

#ifdef _ANDROID
# include <linux/shm.h>
# include <linux/sem.h>
extern int shmctl (int __shmid, int __cmd, struct shmid_ds *__buf);
extern int semget (key_t __key, int __nsems, int __semflg);
extern int semop (int __semid, struct sembuf *__sops, size_t __nsops);
extern int shmdt (__const void *__shmaddr);
extern void *shmat (int __shmid, __const void *__shmaddr, int __shmflg);
extern int shmget (key_t __key, size_t __size, int __shmflg);
extern int semctl (int __semid, int __semnum, int __cmd, ...);
#endif

#include "log.h"
#include "SharedMem.h"

#if (defined(USE_SYSV_SHM) && defined(HAVE_SHMGET)) || defined(_WIN32) || defined(_ANDROID)
# define ENABLE_SHARED_MEM 1
#else
# undef ENABLE_SHARED_MEM
#endif

namespace {
    gnash::RcInitFile& rcfile = gnash::RcInitFile::getDefaultInstance();
}

namespace gnash {

SharedMem::SharedMem(size_t size)
    :
    _addr(0),
    _size(size),
    _semid(0),
    _shmid(0),
    _shmkey(0)
{
}

SharedMem::~SharedMem()
{
    // Nothing to do if we were never attached.
    if (!_addr) return;
#ifndef _WIN32
    if (::shmdt(_addr) < 0) {
        const int err = errno;
        log_error("Error detaching shared memory: %s", std::strerror(err));
    }

    // We can still try to shut it down.
    struct ::shmid_ds ds;
    if (::shmctl(_shmid, IPC_STAT, &ds) < 0) {
        const int err = errno;
        log_error("Error during stat of shared memory segment: %s",
                std::strerror(err));
    }
    else {
        // Note that this isn't completely reliable.
        if (!ds.shm_nattch) {
            log_debug("No shared memory users left. Removing segment.");
            ::shmctl(_shmid, IPC_RMID, 0);
        }
    }
#else
    // Windows code here.
#endif
}

bool
SharedMem::lock()
{
#ifndef _WIN32
    struct sembuf sb = { 0, -1, SEM_UNDO };
    int ret = semop(_semid, &sb, 1);
    return ret >= 0;
#else
    // Windows code here.
    return false;
#endif
}

bool
SharedMem::unlock()
{
#ifndef _WIN32
    struct sembuf sb = { 0, 1, SEM_UNDO };
    int ret = semop(_semid, &sb, 1);
    return ret >= 0;
#else
    // Windows code here
    return false;
#endif
}

bool
SharedMem::attach()
{
#if ENABLE_SHARED_MEM
    
    // Don't try to attach twice.
    if (_addr) return true;
    
    _shmkey = rcfile.getLCShmKey();

    // Check rcfile for key; if there isn't one, use the Adobe key.
    if (_shmkey == 0) {
        log_debug("No shared memory key specified in rcfile. Using default "
                "for communication with other players");
        _shmkey = 0xdd3adabd;
    }
    
    log_debug("Using shared memory key %s",
            boost::io::group(std::hex, std::showbase, _shmkey));

#ifndef _WIN32

    // First get semaphore.
    
    // Struct for semctl
    union semun {
        int val;
        struct semi_ds* buf;
        unsigned short* array;
    };

    // Check if it exists already.
    _semid = semget(_shmkey, 1, 0600);
    
    semun s;

    // If it does not exist, create it and set its value to 1.
    if (_semid < 0) {

        _semid = semget(_shmkey, 1, IPC_CREAT | 0600);
        
        if (_semid < 0) {
            log_error("Failed to get semaphore for shared memory!");
            return false;
        }    

        s.val = 1;
        int ret = semctl(_semid, 0, SETVAL, s);
        if (ret < 0) {
            log_error("Failed to set semaphore value");
            return false;
        }
    }
    
    // The 4th argument is neither necessary nor used, but we pass it
    // anyway for fun.
    int semval = semctl(_semid, 0, GETVAL, s);

    if (semval != 1) {
        log_error("Need semaphore value of 1 for locking. Cannot "
                "attach shared memory!");
        return false;
    }

    Lock lock(*this);

    // Then attach shared memory. See if it exists.
    _shmid = shmget(_shmkey, _size, 0600);

    // If not create it.
    if (_shmid < 0) {
        _shmid = shmget(_shmkey, _size, IPC_CREAT | 0660);
    }

    if (_shmid < 0) {
        log_error("Unable to get shared memory segment!");
        return false;
    }

    _addr = static_cast<iterator>(shmat(_shmid, 0, 0));

    if (!_addr) {
        log_error("Unable to attach shared memory: %s",
                std::strerror(errno));
        return false;
    }

#else

    _shmhandle = CreateFileMapping((HANDLE) 0xFFFFFFFF, NULL,
        PAGE_READWRITE, 0, _size, NULL);

    if (_shmhandle == NULL) {
        log_debug("WARNING: CreateFileMapping failed: %ld\n", GetLastError());
            return false;
    }
    _addr = static_cast<iterator>(MapViewOfFile(_shmhandle, FILE_MAP_ALL_ACCESS,
            0, 0, _size));

    if (!_addr) {
        log_debug("WARNING: MapViewOfFile() failed: %ld\n", GetLastError());
        return false;
    }
#endif

    assert(_addr);
    return true;


#else
# error "You need SYSV Shared memory support to use this option"
#endif
}

} // end of gnash namespace

// Local Variables:
// mode: C++
// indent-tabs-mode: t
// End:
