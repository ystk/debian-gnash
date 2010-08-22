// 
//   Copyright (C) 2010 Free Software Foundation, Inc
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
//

#ifdef HAVE_CONFIG_H
#include "gnashconfig.h"
#endif

#include "log.h"
#include "InputDevice.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/kd.h>

namespace gnash {

static const char *INPUT_DEVICE = "/dev/input/event0";

EventDevice::EventDevice()
{
    // GNASH_REPORT_FUNCTION;
}

EventDevice::EventDevice(Gui *gui)
{
    // GNASH_REPORT_FUNCTION;

    _gui = gui;
}

bool
EventDevice::init()
{
    GNASH_REPORT_FUNCTION;

    return init(INPUT_DEVICE, DEFAULT_BUFFER_SIZE);
}

bool
EventDevice::init(const std::string &filespec, size_t /* size */)
{
    GNASH_REPORT_FUNCTION;
    
    _filespec = filespec;
    
    // Try to open mouse device, be error tolerant (FD is kept open all the time)
    _fd = open(filespec.c_str(), O_RDONLY);
  
    if (_fd < 0) {
        log_debug(_("Could not open %s: %s"), filespec.c_str(), strerror(errno));    
        return false;
    }
  
    if (fcntl(_fd, F_SETFL, fcntl(_fd, F_GETFL) | O_NONBLOCK) < 0) {
        log_error(_("Could not set non-blocking mode for pointing device: %s"),
                  strerror(errno));
        close(_fd);
        _fd = -1;
        return false; 
    }

    // Get the version number of the input event subsystem
    int version;
    if (ioctl(_fd, EVIOCGVERSION, &version)) {
        perror("evdev ioctl");
    }
    log_debug("evdev driver version is %d.%d.%d",
              version >> 16, (version >> 8) & 0xff,
              version & 0xff);
    
    if(ioctl(_fd, EVIOCGID, &_device_info)) {
        perror("evdev ioctl");
    }
    
    char name[256]= "Unknown";
    if(ioctl(_fd, EVIOCGNAME(sizeof(name)), name) < 0) {
        perror("evdev ioctl");
    }
    log_debug("The device on %s says its name is %s", filespec, name);
    
    log_debug("vendor %04hx product %04hx version %04hx",
              _device_info.vendor, _device_info.product,
              _device_info.version);
    switch (_device_info.bustype) {
      case BUS_PCI:
          log_unimpl("is a PCI bus type");
          break;
      case BUS_ISAPNP:
          log_unimpl("is a PNP bus type");
          break;          
      case BUS_USB:
          // FIXME: this probably needs a better way of checking what
          // device things truly are.          
          log_debug("is on a Universal Serial Bus");
          // ID 0eef:0001 D-WAV Scientific Co., Ltd eGalax TouchScreen
          if ((_device_info.product == 0x0001) && (_device_info.vendor == 0x0eef)) {
              _type = InputDevice::TOUCHMOUSE;
              // ID 046d:c001 Logitech, Inc. N48/M-BB48 [FirstMouse Plus]
          } else if ((_device_info.product == 0xc001) && (_device_info.vendor == 0x046d)) {
              _type = InputDevice::MOUSE;
              // ID 0001:0001 AT Translated Set 2 keyboard
          } else if ((_device_info.product == 0x0001) && (_device_info.vendor == 0x0001)) {
              _type = InputDevice::MOUSE;
              // ID 067b:2303 Prolific Technology, Inc. PL2303 Serial Port
          } else if ((_device_info.product == 0x2303) && (_device_info.vendor == 0x067b)) {
              _type = InputDevice::SERIALUSB ;
                  // ID 0471:0815 Philips (or NXP) eHome Infrared Receiver
          } else if ((_device_info.product == 0x0815) && (_device_info.vendor == 0x0471)) {
              _type = InputDevice::INFRARED;
          }
          break;
      case BUS_HIL:
          log_unimpl("is a HIL bus type");
          break;
      case BUS_BLUETOOTH:
          log_unimpl("is Bluetooth bus type ");
          break;
      case BUS_VIRTUAL:
          log_unimpl("is a Virtual bus type ");
          break;
      case BUS_ISA:
          log_unimpl("is an ISA bus type");
          break;
      case BUS_I8042:
          // This is for keyboards and mice
          log_debug("is an I8042 bus type");
          if (strstr(name, "keyboard") != 0) {
              _type = InputDevice::KEYBOARD;
          } else {
              if (strstr(name, "Mouse") != 0) {
                  _type = InputDevice::MOUSE;
              }
          }
          break;
      case BUS_XTKBD:
          log_unimpl("is an XTKBD bus type");
          break;
      case BUS_RS232:
          log_unimpl("is a serial port bus type");
          break;
      case BUS_GAMEPORT:
          log_unimpl("is a gameport bus type");
          break;
      case BUS_PARPORT:
          log_unimpl("is a parallel port bus type");
          break;
      case BUS_AMIGA:
          log_unimpl("is an Amiga bus type");
          break;
      case BUS_ADB:
          log_unimpl("is an AOB bus type");
          break;
      case BUS_I2C:
          log_unimpl("is an i2C bus type ");
          break;
      case BUS_HOST:
          log_debug("is Host bus type");
          _type = InputDevice::POWERBUTTON;
          break;
      case BUS_GSC:
          log_unimpl("is a GSC bus type");
          break;
      case BUS_ATARI:
          log_unimpl("is an Atari bus type");
          break;
      default:
          log_error("Unknown bus type %d!", _device_info.bustype);
    }
    
    log_debug("Event enabled for %s on fd #%d", _filespec, _fd);
    
    return true;
}

bool
EventDevice::check()
{
    GNASH_REPORT_FUNCTION;
    
    bool activity = false;
  
    if (_fd < 0) {
        return false;           // no device
    }

    // Try to read something from the device
    boost::shared_array<boost::uint8_t> buf = readData(sizeof( struct input_event));
    // time,type,code,value
    if (!buf) {
        return false;
    }
    
    struct input_event *ev = reinterpret_cast<struct input_event *>(buf.get());
    // log_debug("Type is: %hd, Code is: %hd, Val us: %d", ev->type, ev->code,
    //           ev->value);
    switch (ev->type) {
      case EV_SYN:
          log_unimpl("Sync event from Input Event Device");
          break;
        // Keyboard event
      case EV_KEY:
      {
          // code == scan code of the key (KEY_xxxx defines in input.h)         
          // value == 0  key has been released
          // value == 1  key has been pressed
          // value == 2  repeated key reporting (while holding the key)
          if (ev->code == KEY_LEFTSHIFT) {
              keyb_lshift = ev->value;
          } else if (ev->code == KEY_RIGHTSHIFT) {
              keyb_rshift = ev->value;
          } else if (ev->code == KEY_LEFTCTRL) {
              keyb_lctrl = ev->value;
          } else if (ev->code == KEY_RIGHTCTRL) {
              keyb_rctrl = ev->value;
          } else if (ev->code == KEY_LEFTALT) {
              keyb_lalt = ev->value;
          } else if (ev->code == KEY_RIGHTALT) {
              keyb_ralt = ev->value;
          } else {
              gnash::key::code  c = scancode_to_gnash_key(ev->code,
                                            keyb_lshift || keyb_rshift);
                  // build modifier
              int modifier = gnash::key::GNASH_MOD_NONE;
              
              if (keyb_lshift || keyb_rshift) {
                  modifier = modifier | gnash::key::GNASH_MOD_SHIFT;
              }
              
              if (keyb_lctrl || keyb_rctrl) {
                  modifier = modifier | gnash::key::GNASH_MOD_CONTROL;
              }
              
              if (keyb_lalt || keyb_ralt) {
                  modifier = modifier | gnash::key::GNASH_MOD_ALT;
              }
              
              // send event
              if (c != gnash::key::INVALID) {
                  _gui->notify_key_event(c, modifier, ev->value);
                  activity = true;
              }
          } // if normal key
          break;
      } // case EV_KEY
      // Mouse
      case EV_REL:
          log_unimpl("Relative move event from Input Event Device");
          // Touchscreen or joystick
          break;
      case EV_ABS:
          log_unimpl("Absolute move event from Input Event Device");
          break;
      case EV_MSC:
          log_unimpl("Misc event from Input Event Device");
          break;
      case EV_LED:
          log_unimpl("LED event from Input Event Device");
          break;
      case EV_SND:
          log_unimpl("Sound event from Input Event Device");
          break;
      case EV_REP:
          log_unimpl("Key autorepeat event from Input Event Device");
          break;
      case EV_FF:
          log_unimpl("Force Feedback event from Input Event Device");
          break;
      case EV_FF_STATUS:  
          log_unimpl("Force Feedback status event from Input Event Device");
          break;
      case EV_PWR:
          log_unimpl("Power event from Input Event Device");
          break;
    }

    return activity;
}

gnash::key::code
EventDevice::scancode_to_gnash_key(int code, bool shift)
{ 
    // NOTE: Scancodes are mostly keyboard oriented (ie. Q, W, E, R, T, ...)
    // while Gnash codes are mostly ASCII-oriented (A, B, C, D, ...) so no
    // direct conversion is possible.
    
    // TODO: This is a very *incomplete* list and I also dislike this method
    // very much because it depends on the keyboard layout (ie. pressing "Z"
    // on a german keyboard will print "Y" instead). So we need some 
    // alternative...
    
    switch (code) {
      case KEY_1      : return !shift ? gnash::key::_1 : gnash::key::EXCLAM;
      case KEY_2      : return !shift ? gnash::key::_2 : gnash::key::DOUBLE_QUOTE; 
      case KEY_3      : return !shift ? gnash::key::_3 : gnash::key::HASH; 
      case KEY_4      : return !shift ? gnash::key::_4 : gnash::key::DOLLAR; 
      case KEY_5      : return !shift ? gnash::key::_5 : gnash::key::PERCENT; 
      case KEY_6      : return !shift ? gnash::key::_6 : gnash::key::AMPERSAND; 
      case KEY_7      : return !shift ? gnash::key::_7 : gnash::key::SINGLE_QUOTE; 
      case KEY_8      : return !shift ? gnash::key::_8 : gnash::key::PAREN_LEFT; 
      case KEY_9      : return !shift ? gnash::key::_9 : gnash::key::PAREN_RIGHT; 
      case KEY_0      : return !shift ? gnash::key::_0 : gnash::key::ASTERISK;
                            
      case KEY_A      : return shift ? gnash::key::A : gnash::key::a;
      case KEY_B      : return shift ? gnash::key::B : gnash::key::b;
      case KEY_C      : return shift ? gnash::key::C : gnash::key::c;
      case KEY_D      : return shift ? gnash::key::D : gnash::key::d;
      case KEY_E      : return shift ? gnash::key::E : gnash::key::e;
      case KEY_F      : return shift ? gnash::key::F : gnash::key::f;
      case KEY_G      : return shift ? gnash::key::G : gnash::key::g;
      case KEY_H      : return shift ? gnash::key::H : gnash::key::h;
      case KEY_I      : return shift ? gnash::key::I : gnash::key::i;
      case KEY_J      : return shift ? gnash::key::J : gnash::key::j;
      case KEY_K      : return shift ? gnash::key::K : gnash::key::k;
      case KEY_L      : return shift ? gnash::key::L : gnash::key::l;
      case KEY_M      : return shift ? gnash::key::M : gnash::key::m;
      case KEY_N      : return shift ? gnash::key::N : gnash::key::n;
      case KEY_O      : return shift ? gnash::key::O : gnash::key::o;
      case KEY_P      : return shift ? gnash::key::P : gnash::key::p;
      case KEY_Q      : return shift ? gnash::key::Q : gnash::key::q;
      case KEY_R      : return shift ? gnash::key::R : gnash::key::r;
      case KEY_S      : return shift ? gnash::key::S : gnash::key::s;
      case KEY_T      : return shift ? gnash::key::T : gnash::key::t;
      case KEY_U      : return shift ? gnash::key::U : gnash::key::u;
      case KEY_V      : return shift ? gnash::key::V : gnash::key::v;
      case KEY_W      : return shift ? gnash::key::W : gnash::key::w;
      case KEY_X      : return shift ? gnash::key::X : gnash::key::x;
      case KEY_Y      : return shift ? gnash::key::Y : gnash::key::y;
      case KEY_Z      : return shift ? gnash::key::Z : gnash::key::z;

      case KEY_F1     : return gnash::key::F1; 
      case KEY_F2     : return gnash::key::F2; 
      case KEY_F3     : return gnash::key::F3; 
      case KEY_F4     : return gnash::key::F4; 
      case KEY_F5     : return gnash::key::F5; 
      case KEY_F6     : return gnash::key::F6; 
      case KEY_F7     : return gnash::key::F7; 
      case KEY_F8     : return gnash::key::F8; 
      case KEY_F9     : return gnash::key::F9;
      case KEY_F10    : return gnash::key::F10;
      case KEY_F11    : return gnash::key::F11;
      case KEY_F12    : return gnash::key::F12;
    
      case KEY_KP0    : return gnash::key::KP_0; 
      case KEY_KP1    : return gnash::key::KP_1; 
      case KEY_KP2    : return gnash::key::KP_2; 
      case KEY_KP3    : return gnash::key::KP_3; 
      case KEY_KP4    : return gnash::key::KP_4; 
      case KEY_KP5    : return gnash::key::KP_5; 
      case KEY_KP6    : return gnash::key::KP_6; 
      case KEY_KP7    : return gnash::key::KP_7; 
      case KEY_KP8    : return gnash::key::KP_8; 
      case KEY_KP9    : return gnash::key::KP_9;

      case KEY_KPMINUS       : return gnash::key::KP_SUBTRACT;
      case KEY_KPPLUS        : return gnash::key::KP_ADD;
      case KEY_KPDOT         : return gnash::key::KP_DECIMAL;
      case KEY_KPASTERISK    : return gnash::key::KP_MULTIPLY;
      case KEY_KPENTER       : return gnash::key::KP_ENTER;
    
      case KEY_ESC           : return gnash::key::ESCAPE;
      case KEY_MINUS         : return gnash::key::MINUS;
      case KEY_EQUAL         : return gnash::key::EQUALS;
      case KEY_BACKSPACE     : return gnash::key::BACKSPACE;
      case KEY_TAB           : return gnash::key::TAB;
      case KEY_LEFTBRACE     : return gnash::key::LEFT_BRACE;
      case KEY_RIGHTBRACE    : return gnash::key::RIGHT_BRACE;
      case KEY_ENTER         : return gnash::key::ENTER;
      case KEY_LEFTCTRL      : return gnash::key::CONTROL;
      case KEY_SEMICOLON     : return gnash::key::SEMICOLON;
          //case KEY_APOSTROPHE    : return gnash::key::APOSTROPHE;  
          //case KEY_GRAVE         : return gnash::key::GRAVE;
      case KEY_LEFTSHIFT     : return gnash::key::SHIFT;
      case KEY_BACKSLASH     : return gnash::key::BACKSLASH;
      case KEY_COMMA         : return gnash::key::COMMA;
      case KEY_SLASH         : return gnash::key::SLASH;
      case KEY_RIGHTSHIFT    : return gnash::key::SHIFT;
      case KEY_LEFTALT       : return gnash::key::ALT;
      case KEY_SPACE         : return gnash::key::SPACE;
      case KEY_CAPSLOCK      : return gnash::key::CAPSLOCK;
      case KEY_NUMLOCK       : return gnash::key::NUM_LOCK;
          //case KEY_SCROLLLOCK    : return gnash::key::SCROLLLOCK;
    
      case KEY_UP            : return gnash::key::UP;
      case KEY_DOWN          : return gnash::key::DOWN;
      case KEY_LEFT          : return gnash::key::LEFT;
      case KEY_RIGHT         : return gnash::key::RIGHT;
      case KEY_PAGEUP        : return gnash::key::PGUP;
      case KEY_PAGEDOWN      : return gnash::key::PGDN;
      case KEY_INSERT        : return gnash::key::INSERT;
      case KEY_DELETE        : return gnash::key::DELETEKEY;
      case KEY_HOME          : return gnash::key::HOME;
      case KEY_END           : return gnash::key::END;
    
    }
  
    return gnash::key::INVALID;  
}

// This looks in the input event devices
std::vector<boost::shared_ptr<InputDevice> > 
EventDevice::scanForDevices(Gui *gui)
{
    // GNASH_REPORT_FUNCTION;

    struct stat st;

    int total = 0;
    std::vector<boost::shared_ptr<InputDevice> > devices;
    
    // The default path for input event devices.
    char *filespec = strdup("/dev/input/eventX");
    int len = strlen(filespec) - 1;

    // Walk through all the input event devices for the ones that
    // match the type we want. There can be multiple devices of the same
    // type, so we return the ID of the event devices.
    filespec[len] = '0';
    int fd = 0;
    while (fd >= 0) {
        // First see if the file exists
        if (stat(filespec, &st) == 0) {
            // Then see if we can open it
            if ((fd = open(filespec, O_RDWR)) < 0) {
                log_error("You don't have the proper permissions to open %s",
                          filespec);
                // Try the next input event device file
                total++;
                filespec[len] = '0' + total;
                fd = 0;
                continue;
            }
        } else {
            // No more device files to try, so we're done scanning
            break;
        }

        char name[256] = "Unknown";
        if(ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
            perror("evdev ioctl");
        }
        log_debug("The device on %s says its name is %s", filespec, name);

        struct input_id device_info;
        if(ioctl(fd, EVIOCGID, &device_info)) {
            perror("evdev ioctl");
        }
        log_debug("vendor %04hx product %04hx version %04hx",
                  device_info.vendor, device_info.product,
                  device_info.version);
        close(fd);
        boost::shared_ptr<InputDevice> dev;
        dev = boost::shared_ptr<InputDevice>(new EventDevice(gui));
        // For now we only want keyboards, as the mouse interface
        // default of /dev/input/mice supports hotpluging devices,
        // unlike the regular event.
        if (dev->init(filespec, DEFAULT_BUFFER_SIZE)) {
            if ((dev->getType() == InputDevice::KEYBOARD)) {
                devices.push_back(dev);
            }
        }

//        dev->dump();
        
        // setup the next device filespec to try
        total++;
        filespec[len] = '0' + total;
    }
    
    free (filespec);
    
    return devices;
}

// end of namespace
}

// local Variables:
// mode: C++
// indent-tabs-mode: nil
// End:
