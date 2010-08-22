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

#ifndef GNASH_GTKSUP_H
#define GNASH_GTKSUP_H

#ifdef HAVE_CONFIG_H
#include "gnashconfig.h"
#endif

#include "gnash.h"
#include "gtk_glue.h"

#include <string>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#ifdef BUILD_CANVAS
#include "gtk_canvas.h"
#endif

#ifdef GUI_HILDON
extern "C" {
# include <hildon/hildon.h>
}
#endif

#ifdef USE_ALP
# include <alp/bundlemgr.h>
#endif

namespace gnash
{


class GtkGui : public Gui
{
public:

    GtkGui(unsigned long xid, float scale, bool loop, RunResources& r);
    
    virtual ~GtkGui();
    
    /// GUI interface implementation

    virtual bool init(int argc, char **argv[]);
    virtual bool createWindow(int width, int height);
    virtual bool createWindow(const char *title, int width, int height,
                              int xPosition = 0, int yPosition = 0);
    virtual void resizeWindow(int width, int height);

    virtual bool run();

    virtual void quitUI();

    virtual bool createMenu();

    virtual bool createMenuAlt(); //an alternative popup menu
    
    /// Set up callbacks for key, mouse and other GTK events.
    //
    /// Must be called after the drawing area has been added to
    /// a top level window, as it calls setupWindowEvents() to
    /// add key event callbacks to the top level window.
    virtual bool setupEvents();
    virtual void beforeRendering();
    virtual void renderBuffer();
    virtual void setInterval(unsigned int interval);
    virtual void setTimeout(unsigned int timeout);
    
    virtual void setFullscreen();
    virtual void unsetFullscreen();
    
    virtual void hideMenu();

    /// For System.capabilities information.
    virtual double getPixelAspectRatio();
    virtual int getScreenResX();
    virtual int getScreenResY();
    virtual double getScreenDPI();
    
    bool watchFD(int fd);

    /// Grab focus so to receive all key events
    //
    /// Might become a virtual in the base class
    ///
    void grabFocus();

    /// Create a menu bar for the application, attach to our window. 
    //  This should only appear in the standalone player.
    bool createMenuBar();
    void createFileMenu(GtkWidget *obj);
    void createEditMenu(GtkWidget *obj);
    void createViewMenu(GtkWidget *obj);
    void createQualityMenu(GtkWidget *obj);
    void createHelpMenu(GtkWidget *obj);
    void createControlMenu(GtkWidget *obj);
    
    // Display a properties dialogue
    void showPropertiesDialog();
    
    // Display a preferences dialogue
    void showPreferencesDialog();
    
    // Display an About dialogue
    void showAboutDialog();

    void expose(const GdkRegion* region);

    void setInvalidatedRegions(const InvalidatedRanges& ranges);

    bool want_multiple_regions() { return true; }

    virtual void setCursor(gnash_cursor_type newcursor);
    
    virtual bool showMouse(bool show);

    virtual void showMenu(bool show);

    virtual void error(const std::string& msg);

    bool checkX11Extension(const std::string& ext);

private:

#ifdef GUI_HILDON
    HildonProgram *_hildon_program;
#endif

    GtkWidget* _window;
    GtkWidget* _resumeButton;
    
    // A window only for rendering the plugin as fullscreen.
    GtkWidget* _overlay;
    
    // The area rendered into by Gnash
#ifdef BUILD_CANVAS
    GtkWidget* _canvas;
#else
    GtkWidget* _drawingArea;
#endif

    GtkMenu* _popup_menu;
    GtkMenu* _popup_menu_alt;
    GtkWidget* _menubar;
    GtkWidget* _vbox;

    /// Add key press events to the toplevel window.
    //
    /// The plugin fullscreen creates a new top level
    /// window, so this function must be called every time
    /// the drawing area is reparented.
    void setupWindowEvents();

#ifdef USE_SWFTREE
    // Create a tree model for displaying movie info
    GtkTreeModel* makeTreeModel (std::auto_ptr<InfoTree> treepointer);
#endif

    void stopHook();
    void playHook();

    guint _advanceSourceTimer;

    void startAdvanceTimer();

    void stopAdvanceTimer();

};

} // namespace gnash

#endif
