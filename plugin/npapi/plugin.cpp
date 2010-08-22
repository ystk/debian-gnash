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

#include <cstdlib> // getenv
#include <stdlib.h> // putenv
#include <sys/types.h>
#if defined(HAVE_WINSOCK_H) && !defined(__OS2__)
# include <winsock2.h>
# include <windows.h>
#else
# include <netinet/in.h>
# include <arpa/inet.h>
# include <netdb.h>
# include <sys/socket.h>
#endif

#include <boost/format.hpp>

#define MIME_TYPES_HANDLED  "application/x-shockwave-flash"
// The name must be this value to get flash movies that check the
// plugin version to load.
#define PLUGIN_NAME    "Shockwave Flash"
#define MIME_TYPES_DESCRIPTION  MIME_TYPES_HANDLED":swf:"PLUGIN_NAME

// Some javascript plugin detectors use the description
// to decide the flash version to display. They expect the
// form (major version).(minor version) r(revision).
// e.g. "8.0 r99."
#define FLASH_VERSION DEFAULT_FLASH_MAJOR_VERSION"."\
    DEFAULT_FLASH_MINOR_VERSION" r"DEFAULT_FLASH_REV_NUMBER"."

#define PLUGIN_DESCRIPTION \
  "Shockwave Flash "FLASH_VERSION"<br>Gnash "VERSION", the GNU SWF Player. \
  Copyright (C) 2006, 2007, 2008, 2009, 2010 \
  <a href=\"http://www.fsf.org\">Free \
  Software Foundation</a>, Inc. <br> \
  Gnash comes with NO WARRANTY, to the extent permitted by law. \
  You may redistribute copies of Gnash under the terms of the \
  <a href=\"http://www.gnu.org/licenses/gpl.html\">GNU General Public \
  License</a>. For more information about Gnash, see <a \
  href=\"http://www.gnu.org/software/gnash/\"> \
  http://www.gnu.org/software/gnash</a>. \
  <br>\
  Compatible Shockwave Flash "FLASH_VERSION

#include "plugin.h" 
#include "GnashSystemIOHeaders.h"
#include "StringPredicates.h"
#include "external.h"
#include "callbacks.h"
#include "npapi.h"
#include "npruntime.h"
#include "npfunctions.h"
#include "GnashNPVariant.h"

#include <boost/tokenizer.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/format.hpp>
#include <sys/param.h>
#include <csignal>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cerrno>
#include <climits>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

// For scriptable plugin support
#include "pluginScriptObject.h"

extern NPNetscapeFuncs NPNFuncs;
extern NPPluginFuncs NPPFuncs;

namespace gnash {
NPBool plugInitialized = FALSE;
}

/// \brief Return the MIME Type description for this plugin.
char*
NPP_GetMIMEDescription(void)
{
    return const_cast<char *>(MIME_TYPES_DESCRIPTION);
}

static bool waitforgdb = false;
static bool createSaLauncher = false;

static const char* getPluginDescription();
// void GnashLogDebug(const std::string& msg);
// void GnashLogError(const std::string& msg);

static const char*
getPluginDescription() 
{
    static const char* desc = NULL;
    if (!desc) {
        desc = std::getenv("GNASH_PLUGIN_DESCRIPTION");
        if (desc == NULL) desc = PLUGIN_DESCRIPTION;
    }
    return desc;
}

//
// general initialization and shutdown
//

/// \brief Initialize the plugin
///
/// This C++ function gets called once when the plugin is loaded,
/// regardless of how many instantiations there is actually playing
/// movies. So this is where all the one time only initialization
/// stuff goes.
NPError
NS_PluginInitialize()
{

    if ( gnash::plugInitialized ) {
        gnash::log_debug("NS_PluginInitialize called, but ignored (we already initialized)");
        return NPERR_NO_ERROR;
    }

    gnash::log_debug("NS_PluginInitialize call ---------------------------");

    // Browser Functionality Checks

    NPError err = NPERR_NO_ERROR;
    NPBool supportsXEmbed = TRUE;

    // First, check for XEmbed support. The NPAPI Gnash plugin
    // only works with XEmbed, so tell the plugin API to fail if
    // XEmbed is not found.
    err = NPN_GetValue(NULL,NPNVSupportsXEmbedBool,
                       (void *)&supportsXEmbed);

    if (err != NPERR_NO_ERROR || !supportsXEmbed) {
        gnash::log_error("NPAPI ERROR: No xEmbed support in this browser!");
        return NPERR_INCOMPATIBLE_VERSION_ERROR;
    } else {
        gnash::log_debug("xEmbed supported in this browser");
    }

    // GTK is not strictly required, but we do use the Glib main event loop,
    // so lack of GTK means reduced functionality.
    NPNToolkitType toolkit;
    err = NPN_GetValue(NULL, NPNVToolkit, &toolkit);

    if (err != NPERR_NO_ERROR || toolkit != NPNVGtk2) {
        gnash::log_error("NPAPI ERROR: No GTK2 support in this browser! Have version %d", (int)toolkit);
    } else {
        gnash::log_debug("GTK2 supported in this browser");
    }

    /*
    Check for environment variables.
    */
    char* opts = std::getenv("GNASH_OPTIONS");
    if (opts != NULL) {
        gnash::log_debug("GNASH_OPTIONS: %s", opts);
        
        // Should the plugin wait for gdb to be attached?
        if ( strstr(opts, "waitforgdb") ) {
            waitforgdb = true;
        }

        // Should the plugin write a script to invoke
        // the standalone player for debugging ?
        if ( strstr(opts, "writelauncher") ) {
            createSaLauncher = true;
        }

    }

    // Append SYSCONFDIR/gnashpluginrc and ~/.gnashpluginrc to GNASHRC

    std::string newGnashRc("GNASHRC=");

#if !defined(__OS2__ ) && ! defined(__amigaos4__)
    newGnashRc.append(SYSCONFDIR);
    newGnashRc.append("/gnashpluginrc");
#endif

    const char *home = NULL;
#if defined(__amigaos4__)
    //on AmigaOS we have a GNASH: assign that point to program dir
    home = "/gnash";
#elif defined(__HAIKU__)
    BPath bp;
    if (B_OK != find_directory(B_USER_SETTINGS_DIRECTORY, &bp))
    {
        std::cerr << "Failed to find user settings directory" << std::endl;
    } else {
        bp.Append("Gnash");
        home = bp.Path();
    }
#else
    home = std::getenv("HOME");
#endif
    if ( home ) {
        newGnashRc.append(":");
        newGnashRc.append(home);
#ifdef __HAIKU__
        newGnashRc.append("/gnashpluginrc");
#else
        newGnashRc.append("/.gnashpluginrc");
#endif
    } else {
        gnash::log_error("WARNING: NPAPI plugin could not find user home dir");
    }

    char *gnashrc = std::getenv("GNASHRC");
    if ( gnashrc ) {
        newGnashRc.append(":");
        newGnashRc.append(gnashrc);
    }

    // putenv doesn't copy the string in standards-conforming implementations
    gnashrc = new char[PATH_MAX];
    std::strncpy(gnashrc, newGnashRc.c_str(), PATH_MAX);
    gnashrc[PATH_MAX-1] = '\0';

    if ( putenv(gnashrc) ) {
        gnash::log_debug("WARNING: NPAPI plugin could not append to the GNASHRC env variable");
    } else {
        gnash::log_debug("NOTE: NPAPI plugin set GNASHRC to %d", newGnashRc);
    }

    /* Success */

    gnash::plugInitialized = TRUE;

    return NPERR_NO_ERROR;
}

/// \brief Shutdown the plugin
///
/// This C++ function gets called once when the plugin is being
/// shutdown, regardless of how many instantiations actually are
/// playing movies. So this is where all the one time only
/// shutdown stuff goes.
void
NS_PluginShutdown()
{
#if 0
    if (!plugInitialized) {
        gnash::log_debug("Plugin already shut down");
        return;
    }

    plugInitialized = FALSE;
#endif
}

/// \brief Retrieve values from the plugin for the Browser
///
/// This C++ function is called by the browser to get certain
/// information is needs from the plugin. This information is the
/// plugin name, a description, etc...
NPError
NS_PluginGetValue(NPPVariable aVariable, void *aValue)
{
    NPError err = NPERR_NO_ERROR;

    switch (aVariable) {
      case NPPVpluginNameString:
          *static_cast<const char **> (aValue) = PLUGIN_NAME;
          break;
          
          // This becomes the description field you see below the opening
          // text when you type about:plugins and in
          // navigator.plugins["Shockwave Flash"].description, used in
          // many flash version detection scripts.
      case NPPVpluginDescriptionString:
          *static_cast<const char **>(aValue) = getPluginDescription();
          break;

      case NPPVpluginWindowBool:
          break;
          
      case NPPVpluginTimerInterval:
          break;
          
      case NPPVpluginKeepLibraryInMemory:
          break;
          
      case NPPVpluginNeedsXEmbed:
#ifdef HAVE_GTK2
          *static_cast<NPBool *>(aValue) = TRUE;
#else
          *static_cast<NPBool *>(aValue) = FALSE;
#endif
          break;

      case NPPVpluginScriptableNPObject:
          break;

      case NPPVpluginUrlRequestsDisplayedBool:
          break;
      case NPPVpluginWantsAllNetworkStreams:
          break;
          
      default:
          err = NPERR_INVALID_PARAM;
          break;
    }
    return err;
}

/// \brief construct our plugin instance object
///
/// This instantiates a new object via a C++ function used by the
/// browser.
nsPluginInstanceBase *
NS_NewPluginInstance(nsPluginCreateData * aCreateDataStruct)
{
    // gnash::log_debug(__PRETTY_FUNCTION__);

    if(!aCreateDataStruct) {
        return NULL;
    }

    return new gnash::nsPluginInstance(aCreateDataStruct);
}

/// \brief destroy our plugin instance object
///
/// This destroys our instantiated object via a C++ function used by the
/// browser.
void
NS_DestroyPluginInstance(nsPluginInstanceBase* aPlugin)
{
    delete static_cast<gnash::nsPluginInstance *> (aPlugin);
}

namespace gnash {

//
// nsPluginInstance class implementation
//

/// \brief Constructor
nsPluginInstance::nsPluginInstance(nsPluginCreateData* data)
    :
    nsPluginInstanceBase(),
    _instance(data->instance),
    _window(0),
    _width(0),
    _height(0),
    _streamfd(-1),
    _ichan(0),
    _ichanWatchId(0),
    _controlfd(-1),
    _childpid(0),
    _filefd(-1),
    _name(),
    _scriptObject(0)
{
    // gnash::log_debug("%s: %x", __PRETTY_FUNCTION__, (void *)this);

    for (size_t i=0, n=data->argc; i<n; ++i) {
        std::string name, val;
        gnash::StringNoCaseEqual noCaseCompare;

        if (data->argn[i]) {
            name = data->argn[i];
        }

        if (data->argv[i]) {
            val = data->argv[i];
        }

        if (noCaseCompare(name, "name")) {
            _name = val;
        }

        _params[name] = val;
    }

#if 1
    if (NPNFuncs.version >= 14) { // since NPAPI start to support
        _scriptObject = (GnashPluginScriptObject *)NPNFuncs.createobject(
            _instance, GnashPluginScriptObject::marshalGetNPClass());
    }
#endif
    
    return;
}

gboolean
cleanup_childpid(gpointer data)
{
    int* pid = static_cast<int*>(data);

    int status;
    int rv = waitpid(*pid, &status, WNOHANG);

    if (rv <= 0) {
        // The child process has not exited; it may be deadlocked. Kill it.
//        gnash::log_error("BUG: Child process is stuck. Killing it.");

        kill(*pid, SIGKILL);
        waitpid(*pid, &status, 0);
    }
 
    gnash::log_debug("Child process exited with status %s", status);

    delete pid;

    return FALSE;
}

/// \brief Destructor
nsPluginInstance::~nsPluginInstance()
{
//    gnash::log_debug("plugin instance destruction");

    if ( _ichanWatchId ) {
        g_source_remove(_ichanWatchId);
        _ichanWatchId = 0;
    }

    if (_childpid > 0) {
        // When the child has terminated (signaled by _controlfd), it remains
        // as a defunct process and we remove it from the kernel table now.
        
        // If all goes well, Gnash will already have terminated.
        int status;
        int rv = waitpid(_childpid, &status, WNOHANG);

        if (rv <= 0) {
             int* pid = new int(_childpid);
	     usleep(1000);
	     cleanup_childpid(pid);
        } else {
            gnash::log_debug("Child process exited with status %d", status);
        }
    }
    _childpid = 0;
}

/// \brief Initialize an instance of the plugin object
/// 
/// This methods initializes the plugin object, and is called for
/// every movie that gets played. This is where the movie playing
/// specific initialization goes.
NPBool
nsPluginInstance::init(NPWindow* aWindow)
{
    if(!aWindow) {
        gnash::log_error("%s: ERROR: Window handle was bogus!", __PRETTY_FUNCTION__);
        return FALSE;
    } else {
#if GNASH_PLUGIN_DEBUG > 1
        std::cout << "X origin: = " << aWindow->x 
            << ", Y Origin = " << aWindow->y
            << ", Width = " << aWindow->width
            << ", Height = " << aWindow->height
            << ", WindowID = " << aWindow->window
            << ", this = " << static_cast<void*>(this) << std::endl;
#endif
    }

    return TRUE;
}

/// \brief Shutdown an instantiated object
///
/// This shuts down an object, and is called for every movie that gets
/// played. This is where the movie playing specific shutdown code
/// goes.
void
nsPluginInstance::shut()
{
    gnash::log_debug("Gnash plugin shutting down");

    if (_streamfd != -1) {
        if (close(_streamfd) == -1) {
            perror("closing _streamfd");
        } else {
            _streamfd = -1;
        }
    }

    if (_controlfd != -1) {
        _scriptObject->closePipe(_controlfd);
        if (close(_controlfd) != 0) {
            gnash::log_error("Gnash plugin failed to close the control socket!");
        }
    }


}
/// \brief Set the window to be used to render in
///
/// This sets up the window the plugin is supposed to render
/// into. This calls passes in various information used by the plugin
/// to setup the window. This may get called multiple times by each
/// instantiated object, so it can't do much but window specific
/// setup here.
NPError
nsPluginInstance::SetWindow(NPWindow* aWindow)
{
    if(!aWindow) {
        gnash::log_error(std::string(__FUNCTION__) + ": ERROR: Window handle was bogus!");
        return NPERR_INVALID_PARAM;
#if 0
    } else {
        gnash::log_debug("%s: X origin = %d, Y Origin = %d, Width = %d,"
            " Height = %d, WindowID = %p, this = %p",
            __FUNCTION__,
            aWindow->x, aWindow->y, aWindow->width, aWindow->height,
            aWindow->window, this);
#endif
    }

    if (_window) {
        return NPERR_GENERIC_ERROR;
    }

    _width = aWindow->width;
    _height = aWindow->height;

    _window = reinterpret_cast<Window> (aWindow->window);

    // When testing the interface to the plugin, don't start the player
    // as a debug client "nc -l 1111" is used instead.
    if (!_childpid && !_swf_url.empty()) {
        startProc();
    }

    return NPERR_NO_ERROR;
}

NPError
nsPluginInstance::GetValue(NPPVariable aVariable, void *aValue)
{

    if (aVariable == NPPVpluginScriptableNPObject) {
        if (_scriptObject) {
            void **v = (void **)aValue;
            NPNFuncs.retainobject(_scriptObject);
            *v = _scriptObject;
        } else {
            gnash::log_debug("_scriptObject is not assigned");
        }
    }    

    // log_debug("SCRIPT OBJECT getValue: %x, ns: %x", (void *)_scriptObject, (void *)this);

    return NS_PluginGetValue(aVariable, aValue);
}

#if 0
// FIXME: debugging stuff, will be gone soon after I figure how this works
void myfunc(void */* param */)
{
    gnash::log_debug("Here I am!!!\n");
}
#endif

/// \brief Open a new data stream
///
/// Opens a new incoming data stream, which is the flash movie we want
/// to play.
/// A URL can be pretty ugly, like in this example:
/// http://www.shockwave.com/swf/navbar/navbar_sw.swf?atomfilms=http%3a//www.atomfilms.com/af/home/&shockwave=http%3a//www.shockwave.com&gameblast=http%3a//gameblast.shockwave.com/gb/gbHome.jsp&known=0
/// ../flash/gui.swf?ip_addr=foobar.com&ip_port=3660&show_cursor=true&path_prefix=../flash/&trapallkeys=true"
///

NPError
nsPluginInstance::NewStream(NPMIMEType /*type*/, NPStream* stream,
                            NPBool /*seekable*/, uint16_t* /*stype*/)
{
    // gnash::log_debug("%s: %x", __PRETTY_FUNCTION__, (void *)this);

    if (_childpid) {
        // Apparently the child process has already been started for this
        // plugin instance. It is puzzling that this method gets called
        // again. Starting a new process for the same movie will cause
        // problems later, so we'll stop here.
        return NPERR_GENERIC_ERROR;
    }
    _swf_url = stream->url;

#if 0
    // FIXME: debugging crap for now call javascript
    NPN_PluginThreadAsyncCall(_instance, myfunc, NULL);
    // gnash::log_debug("FIXME: %s", getEmbedURL());
#endif
    
    if (!_swf_url.empty() && _window) {
        startProc();
    }

    return NPERR_NO_ERROR;
}

/// \brief Destroy the data stream we've been reading.
NPError
nsPluginInstance::DestroyStream(NPStream* /*stream*/, NPError /*reason*/)
{

    if (_streamfd != -1) {
        if (close(_streamfd) == -1) {
            perror("closing _streamfd");
        } else {
            _streamfd = -1;
        }
    }

    return NPERR_NO_ERROR;
}

/// \brief Return how many bytes we can read into the buffer
int32_t
nsPluginInstance::WriteReady(NPStream* /* stream */ )
{
    //gnash::log_debug("Stream for %s is ready", stream->url);
    if ( _streamfd != -1 ) {
	return 1024;
    } else {
	return 0;
    }
}

/// \brief Read the data stream from Mozilla/Firefox
///
int32_t
nsPluginInstance::Write(NPStream* /*stream*/, int32_t /*offset*/, int32_t len,
        void* buffer)
{
    int written = write(_streamfd, buffer, len);
    return written;
}

bool
nsPluginInstance::handlePlayerRequestsWrapper(GIOChannel* iochan,
        GIOCondition cond, nsPluginInstance* plugin)
{
    return plugin->handlePlayerRequests(iochan, cond);
}

bool
nsPluginInstance::handlePlayerRequests(GIOChannel* iochan, GIOCondition cond)
{
    // gnash::log_debug("%s: %d: %x", __PRETTY_FUNCTION__, __LINE__, (void *)this);

    if ( cond & G_IO_HUP ) {
        gnash::log_debug("Player control socket hang up");
        // Returning false here will cause the "watch" to be removed. This watch
        // is the only reference held to the GIOChannel, so it will be
        // destroyed. We must make sure we don't attempt to destroy it again.
        _ichanWatchId = 0;
        return false;
    }

    assert(cond & G_IO_IN);

    gnash::log_debug("Checking player requests on fd #%d",
              g_io_channel_unix_get_fd(iochan));

    GError* error = 0;
    //    g_io_channel_set_flags(iochan, G_IO_FLAG_NONBLOCK, &error);

    size_t retries = 5;
    gchar* request = 0;
    gsize requestSize = 0;
    do {
        // When in non-blocking mode, we'll get several iterations of this
        // loop while waiting for data. if data never arrives, we'd be stuck
        // looping here forever, so this is our escape from that loop.
        if (retries-- <= 0) {
            gnash::log_error("Too many attempts to read from the player!");
            return false;
        }
        error = 0;
        request = 0;
        requestSize = 0;
        GIOStatus status = g_io_channel_read_line(iochan, &request,
                           &requestSize, NULL, &error);
        switch (status) {
          case G_IO_STATUS_ERROR:
              gnash::log_error("error reading request line: %s",
                               error->message);
              g_error_free(error);
              return false;
          case G_IO_STATUS_EOF:
              gnash::log_error("EOF (error: %s", error->message);
              g_error_free(error);
              return false;
          case G_IO_STATUS_AGAIN:
              gnash::log_debug("read again: nonblocking mode set ");
              continue;
          case G_IO_STATUS_NORMAL:
              // process request
              // Get rid of the newline on the end if there is one. The string
              // is also NULL terninated, so the requestSize includes it in
              // the total.
              if (request[requestSize-1] == '\n') {
                  request[requestSize-1] = 0;
                  requestSize--;
              }
              gnash::log_debug("Normal read: %s", request);
              break;
          default:
              gnash::log_error("Abnormal status!");
              return false;
        }
    } while (g_io_channel_get_buffer_condition(iochan) & G_IO_IN);

    // process request..
    processPlayerRequest(request, requestSize);
    g_free(request);
    
    return true;
}

bool
nsPluginInstance::processPlayerRequest(gchar* buf, gsize linelen)
{
    // gnash::log_debug(__PRETTY_FUNCTION__);

   // log_debug("SCRIPT OBJECT %d: %x", __LINE__, this->getScriptObject());

    if ( linelen < 4 ) {
        if (buf) {
            gnash::log_error("Invalid player request (too short): %s", buf);
        } else {
            gnash::log_error("Invalid player request (too short): %d bytes", linelen);
        }
        return false;
    }
    
    ExternalInterface::invoke_t *invoke = ExternalInterface::parseInvoke(buf);

    if (!invoke->name.empty()) {
        gnash::log_debug("Requested method is: %s", invoke->name);
    }
    
    // The invoke message is also used for getURL. In this case there are 4
    // possible arguments.
    if (invoke) {
        if (invoke->name == "getURL") {
            // gnash::log_debug("Got a getURL() request: %", invoke->args[0].get());

            // The first argument is the URL string.
            std::string url = NPStringToString(NPVARIANT_TO_STRING(
                                                   invoke->args[0].get()));
            // The second is the method, namely GET or POST.
            std::string op = NPStringToString(NPVARIANT_TO_STRING(
                                                  invoke->args[1].get()));
            // The third is the optional target, which is something like
            // _blank or _self. NONE means no target.
            std::string target;
            // The fourth is the optional data. If there is data, the target
            // field is always set so this argument is on the correct index.
            // No target is "NONE".
            std::string data;
            if (invoke->args.size() >= 3) {
                target = NPStringToString(NPVARIANT_TO_STRING(
                                              invoke->args[2].get()));
                if (target == "NONE") {
                    target.clear();
                }
            }
            if (invoke->args.size() == 4) {
                data = NPStringToString(NPVARIANT_TO_STRING(
                                            invoke->args[3].get()));
            }
            if (op == "GET") {
                gnash::log_debug("Asked to getURL '%s' in target %s", url,
                                 target);
                NPN_GetURL(_instance, url.c_str(), target.c_str());
            } else if (op == "POST") {                
                gnash::log_debug("Asked to postURL '%s' this data %s", url,
                                 data);
                NPN_PostURL(_instance, url.c_str(), target.c_str(), data.size(),
                            data.c_str(), false);
                return true;
            }
            
            return true;
        } else if (invoke->name == "fsCommand") {
            std::string command = NPStringToString(NPVARIANT_TO_STRING(
                                                       invoke->args[0].get()));
            std::string arg = NPStringToString(NPVARIANT_TO_STRING(
                                                   invoke->args[1].get()));            
            std::string name = _name; 
            std::stringstream jsurl;
            jsurl << "javascript:" << name << "_DoFSCommand('" << command
                  << "','" << arg <<"')";
            
            // TODO: check if _self is a good target for this
            static const char* tgt = "_self";
            
            gnash::log_debug("Calling NPN_GetURL(%s, %s)",
                             jsurl.str(), tgt);
            
            NPN_GetURL(_instance, jsurl.str().c_str(), tgt);
            return true;
        } else if (invoke->name == "addMethod") {
            // Make this flash function accessible to Javascript. The
            // actual callback lives in libcore/movie_root, but it
            // needs to be on the list of supported remote methods so
            // it can be called by Javascript.
            std::string method = NPStringToString(NPVARIANT_TO_STRING(
                                                      invoke->args[0].get()));
            NPIdentifier id = NPN_GetStringIdentifier(method.c_str());
            // log_debug("SCRIPT OBJECT addMethod: %x, %s", (void *)_scriptObject, method);
            this->getScriptObject()->AddMethod(id, remoteCallback);
            return true;
        }
        NPVariant *result = 0;
        // This is the player invoking a method in Javascript
        uint32_t count = 0;
        NPVariant args;
        if (!invoke->name.empty()) {
            NPIdentifier id = NPN_GetStringIdentifier(invoke->name.c_str());      
            gnash::log_debug("Invoking JavaScript method %s", invoke->name);
            NPN_Invoke(_instance, _scriptObject, id, &args, count, result);
        }
        // We got a result from invoking the Javascript method
        std::stringstream ss;
        if (result) {
            NPN_ReleaseVariantValue(result);
            ss << ExternalInterface::convertNPVariant(result);
        } else {
            // Send response
            // FIXME: "securityError" also possible, check domain
            ss << ExternalInterface::makeString("Error");
        }
        size_t ret = _scriptObject->writePlayer(ss.str());
        if (ret != ss.str().size()) {
            log_error("Couldn't write the response to Gnash, network problems.");
            return false;
        }
        return true;
    } else {
        gnash::log_error("Unknown player request: " + std::string(buf));
        return false;
    }

    return false;
}

std::string
getGnashExecutable()
{
    std::string procname;
    bool process_found = false;
    struct stat procstats;

    char *gnash_env = std::getenv("GNASH_PLAYER");

    if (gnash_env) {
        procname = gnash_env;
        process_found = (0 == stat(procname.c_str(), &procstats));
        if (!process_found) {
            gnash::log_error("Invalid path to gnash executable: ");
            return "";
        }
    }

    if (!process_found) {
        procname = GNASHBINDIR "/gtk-gnash";
        process_found = (0 == stat(procname.c_str(), &procstats));
    }
    if (!process_found) {
        procname = GNASHBINDIR "/kde4-gnash";
        process_found = (0 == stat(procname.c_str(), &procstats));
    }

    if (!process_found) {
        gnash::log_error(std::string("Unable to find Gnash in ") + GNASHBINDIR);
        return "";
    }


    return procname;
}

void
create_standalone_launcher(const std::string& page_url, const std::string& swf_url,
                           const std::map<std::string, std::string>& params)
{
#ifdef CREATE_STANDALONE_GNASH_LAUNCHER
    if (!createSaLauncher) {
        return;
    }

    std::ofstream saLauncher;

    std::stringstream ss;
    static int debugno = 0;
    debugno = (debugno + 1) % 10;
    ss << "/tmp/gnash-debug-" << debugno << ".sh";
    saLauncher.open(ss.str().c_str(), std::ios::out | std::ios::trunc);

    if (!saLauncher) {
        gnash::log_error("Failed to open new file for standalone launcher: " + ss.str());
        return;
    }

    saLauncher << "#!/bin/sh" << std::endl
               << getGnashExecutable() << " ";

    if (!page_url.empty()) {
        saLauncher << "-U '" << page_url << "' ";
    }

    for (std::map<std::string,std::string>::const_iterator it = params.begin(),
        itEnd = params.end(); it != itEnd; ++it) {
        const std::string& nam = it->first; 
        const std::string& val = it->second;
        saLauncher << "-P '" << nam << "=" << val << "' ";
    }

    saLauncher << "'" << swf_url << "' "
               << "$@"       // allow caller to pass any additional argument
               << std::endl;

    saLauncher.close();
#endif
}

std::vector<std::string>
nsPluginInstance::getCmdLine(int hostfd, int controlfd)
{
    std::vector<std::string> arg_vec;

    std::string cmd = getGnashExecutable();
    if (cmd.empty()) {
        gnash::log_error("Failed to locate the Gnash executable!");
        return arg_vec;
    }
    arg_vec.push_back(cmd);

    arg_vec.push_back("-u");
    arg_vec.push_back(_swf_url);
    
    std::string pageurl = getCurrentPageURL();
    if (pageurl.empty()) {
        gnash::log_error("Could not get current page URL!");
    } else {
        arg_vec.push_back("-U");
        arg_vec.push_back(pageurl);
    }

    // In pre xulrunner 1.9, (Firefox 3.1) this function does not exist,
    // so we can't use it to read the cookie file. For older browsers
    // like IceWeasel on Debian lenny, which pre dates the cookie support
    // in NPAPI, you have to block all Cookie for sites like YouTube to
    // allow Gnash to work.
    if (NPNFuncs.getvalueforurl) {
        // Cookie appear to drop anything past the domain, so we strip
        // that off.
        std::string::size_type pos;
        pos = pageurl.find("/", pageurl.find("//", 0) + 2) + 1;
        std::string url = pageurl.substr(0, pos);
        
        char *cookie = 0;
        uint32_t length = 0;
        NPN_GetValueForURL(_instance, NPNURLVCookie, url.c_str(),
                           &cookie, &length);
        std::string ncookie (cookie, length);
        if (cookie) {
            gnash::log_debug("The Cookie for %s is %s", url, ncookie);
            std::ofstream cookiefile;
            std::stringstream ss;
            ss << "/tmp/gnash-cookies." << getpid(); 
            
            cookiefile.open(ss.str().c_str(), std::ios::out | std::ios::trunc);
            cookiefile << "Set-Cookie: " << ncookie << std::endl;
            cookiefile.close();
            
            if (setenv("GNASH_COOKIES_IN", ss.str().c_str(), 1) < 0) {
                gnash::log_error(
                    "Couldn't set environment variable GNASH_COOKIES_IN to %s",
                    ncookie);
            }
            NPN_MemFree(cookie);
        } else {
            gnash::log_debug("No stored Cookie for %s", url);
        }    
    }

    std::stringstream pars;
    pars << "-x "  <<  _window          // X window ID to render into
         << " -j " << _width            // Width of window
         << " -k " << _height;           // Height of window
#if GNASH_PLUGIN_DEBUG > 1
    pars << " -vv ";
#endif
    if ((hostfd > 0) && (controlfd)) {
        pars << " -F " << hostfd            // Socket to send commands to
             << ":"    << controlfd;        // Socket determining lifespan
    }
    std::string pars_str = pars.str();
    typedef boost::char_separator<char> char_sep;
    boost::tokenizer<char_sep> tok(pars_str, char_sep(" "));
    arg_vec.insert(arg_vec.end(), tok.begin(), tok.end());

    for (std::map<std::string,std::string>::const_iterator it = _params.begin(),
        itEnd = _params.end(); it != itEnd; ++it) {
        const std::string& nam = it->first; 
        const std::string& val = it->second;
        arg_vec.push_back("-P");
        arg_vec.push_back(nam + "=" + val);
    }
    arg_vec.push_back("-");

    create_standalone_launcher(pageurl, _swf_url, _params);

    return arg_vec;
}

template <std::size_t N>
void
close_fds(const int (& except)[N])
{
    // Rather than close all the thousands of possible file
    // descriptors, we start after stderr and keep closing higher numbers
    // until we encounter ten fd's in a row that
    // aren't open. This will tend to close most fd's in most programs.
    int numfailed = 0, closed = 0;
    for (int anfd = fileno(stderr)+1; numfailed < 10; anfd++) {
        if (std::find(except, except+N, anfd) != except+N) {
            continue;
        }
        if (close(anfd) < 0) {
            numfailed++;
        } else {
            numfailed = 0;
            closed++;
        }
    }
    gnash::log_debug("Closed %d files.", closed);
} 

void
wait_for_gdb()
{
    if (!waitforgdb) {
        return;
    }

    // For debugging the plugin (GNASH_OPTIONS=waitforgdb)
    // Block here until gdb is attached and sets waitforgdb to false.

    std::cout << std::endl << "  Attach GDB to PID " << getpid()
              << " to debug!" << std::endl
              << "  This thread will block until then!" << std::endl
              << "  Once blocked here, you can set other breakpoints."
              << std::endl
              << "  Do a \"set variable waitforgdb=$false\" to continue"
              << std::endl << std::endl;

    while (waitforgdb) {
        sleep(1);
    }
}

void
nsPluginInstance::startProc()
{

    int p2c_pipe[2];
    int c2p_pipe[2];
    int p2c_controlpipe[2];

    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, p2c_pipe);
    if (ret == -1) {
        gnash::log_error("ERROR: socketpair(p2c) failed: %s", strerror(errno));
        return;
    }
    _streamfd = p2c_pipe[1];

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, c2p_pipe);
    if (ret == -1) {
        gnash::log_error("ERROR: socketpair(c2p) failed: %s", strerror(errno));
        return;
    }

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, p2c_controlpipe);
    if (ret == -1) {
        gnash::log_error("ERROR: socketpair(control) failed: %s", strerror(errno));
        return;
    }

    _scriptObject->setControlFD(p2c_controlpipe[1]);
    _scriptObject->setHostFD(c2p_pipe[0]);
    
    // Setup the command line for starting Gnash

    std::vector<std::string> arg_vec = getCmdLine(c2p_pipe[1],
                                                  p2c_controlpipe[0]);

    if (arg_vec.empty()) {
        gnash::log_error("Failed to obtain command line parameters.");
        return;
    }
    
    std::vector<const char*> args;
    
    std::transform(arg_vec.begin(), arg_vec.end(), std::back_inserter(args),
                   std::mem_fun_ref(&std::string::c_str));
    args.push_back(0);
    
    // Argument List prepared, now fork(), close file descriptors and execv()
    
    _childpid = fork();
    
    // If the fork failed, childpid is -1. So print out an error message.
    if (_childpid == -1) {
        gnash::log_error("ERROR: dup2() failed: " + std::string(strerror(errno)));
        return;
    }
    
    // If we are the parent and fork() worked, childpid is a positive integer.
    if (_childpid > 0) {
        
        // we want to write to p2c pipe, so close read-fd0
        ret = close (p2c_pipe[0]);
        if (ret == -1) {
            // this is not really a fatal error, so continue best as we can
            gnash::log_error("ERROR: p2c_pipe[0] close() failed: %s",
                             strerror(errno));
        }
        
        // we want to read from c2p pipe, so close read-fd1
        ret = close (c2p_pipe[1]);
        if (ret == -1) {
            // this is not really a fatal error, so continue best as we can
            gnash::log_error("ERROR: c2p_pipe[1] close() failed: %s",
                             strerror(errno));
            gnash::log_debug("Forked successfully but with ignorable errors.");
        } else {
            gnash::log_debug("Forked successfully, child process PID is %d",
                             _childpid);
        }
        
        GIOChannel* ichan = g_io_channel_unix_new(c2p_pipe[0]);
        gnash::log_debug("New IO Channel for fd #%d",
                         g_io_channel_unix_get_fd(ichan));
        g_io_channel_set_close_on_unref(ichan, true);
        _ichanWatchId = g_io_add_watch(ichan, 
                                       (GIOCondition)(G_IO_IN|G_IO_HUP), 
                                       (GIOFunc)handlePlayerRequestsWrapper,
                                       this);
        return;
    }
    
    // This is the child scope.
    
    // FF3 uses jemalloc and it has problems after the fork(), do NOT
    // use memory functions (malloc()/free()/new/delete) after the fork()
    // in the child thread process
    ret = close (p2c_pipe[1]);
    
    // close standard input and direct read-fd1 to standard input
    ret = dup2 (p2c_pipe[0], fileno(stdin));

    if (ret == -1) {
        gnash::log_error("ERROR: dup2() failed: " + std::string(strerror(errno)));
    }
    
    // Close all of the browser's file descriptors that we just inherited
    // (including p2c_pipe[0] that we just dup'd to fd 0), but obviously
    // not the file descriptors that we want the child to use.
    int dontclose[] = {c2p_pipe[1], c2p_pipe[0], p2c_controlpipe[0]};
    close_fds(dontclose);

    // Start the desired executable and go away.
    
    gnash::log_debug("Starting process: %s", boost::algorithm::join(arg_vec, " "));

    wait_for_gdb();

    execv(args[0], const_cast<char**>(&args.front()));

    // if execv returns, an error has occurred.
    perror("executing standalone gnash");
    
    exit (-1);
}

std::string
nsPluginInstance::getCurrentPageURL() const
{
    NPP npp = _instance;

    NPIdentifier sDocument = NPN_GetStringIdentifier("document");

    NPObject *window;
    NPN_GetValue(npp, NPNVWindowNPObject, &window);

    NPVariant vDoc;
    NPN_GetProperty(npp, window, sDocument, &vDoc);
    NPN_ReleaseObject(window);

    if (!NPVARIANT_IS_OBJECT(vDoc)) {
        gnash::log_error("Can't get window object");
        return NULL;
    }
    
    NPObject* npDoc = NPVARIANT_TO_OBJECT(vDoc);

    NPIdentifier sLocation = NPN_GetStringIdentifier("location");
    NPVariant vLoc;
    NPN_GetProperty(npp, npDoc, sLocation, &vLoc);
    NPN_ReleaseObject(npDoc);

    if (!NPVARIANT_IS_OBJECT(vLoc)) {
        gnash::log_error("Can't get window.location object");
        return NULL;
    }

    NPObject* npLoc = NPVARIANT_TO_OBJECT(vLoc);

    NPIdentifier sProperty = NPN_GetStringIdentifier("href");
    NPVariant vProp;
    NPN_GetProperty(npp, npLoc, sProperty, &vProp);
    NPN_ReleaseObject(npLoc);

    if (!NPVARIANT_IS_STRING(vProp)) {
        gnash::log_error("Can't get window.location.href object");
        return NULL;
    }

    const NPString& propValue = NPVARIANT_TO_STRING(vProp);

    return NPStringToString(propValue);
}

void
processLog_error(const boost::format& fmt)
{
    std::cerr << "ERROR: " << fmt.str() << std::endl;
}

#if GNASH_PLUGIN_DEBUG > 1
void
processLog_debug(const boost::format& fmt)
{
    std::cout << "DEBUG: " << fmt.str() << std::endl;
}

void
processLog_trace(const boost::format& fmt)
{
    std::cout << "TRACE: " << fmt.str() << std::endl;
}
#else
void
processLog_debug(const boost::format& /* fmt */)
{ /* do nothing */ }

void
processLog_trace(const boost::format& /* fmt */)
{ /* do nothing */ }
#endif

} // end of gnash namespace

// Local Variables:
// mode: C++
// indent-tabs-mode: nil
// End:
