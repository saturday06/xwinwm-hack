// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// blackbox.cc for Blackbox - an X11 Window manager
// Copyright (c) 2003 Kensuke Matsuzaki <zakki@peppermint.jp>
// Copyright (c) 2001 - 2002 Sean 'Shaleh' Perry <shaleh@debian.org>
// Copyright (c) 1997 - 2000 Brad Hughes (bhughes@tcac.net)
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#ifdef    HAVE_CONFIG_H
#  include "../config.h"
#endif // HAVE_CONFIG_H

extern "C" {
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

#include <X11/extensions/windowswm.h>
#include <X11/extensions/windowswmstr.h>

#ifdef    SHAPE
#include <X11/extensions/shape.h>
#endif // SHAPE

#ifdef    HAVE_STDIO_H
#  include <stdio.h>
#endif // HAVE_STDIO_H

#ifdef HAVE_STDLIB_H
#  include <stdlib.h>
#endif // HAVE_STDLIB_H

#ifdef HAVE_STRING_H
#  include <string.h>
#endif // HAVE_STRING_H

#ifdef    HAVE_UNISTD_H
#  include <sys/types.h>
#  include <unistd.h>
#endif // HAVE_UNISTD_H

#ifdef    HAVE_SYS_PARAM_H
#  include <sys/param.h>
#endif // HAVE_SYS_PARAM_H

#ifdef    HAVE_SYS_SELECT_H
#  include <sys/select.h>
#endif // HAVE_SYS_SELECT_H

#ifdef    HAVE_SIGNAL_H
#  include <signal.h>
#endif // HAVE_SIGNAL_H

#ifdef    HAVE_SYS_SIGNAL_H
#  include <sys/signal.h>
#endif // HAVE_SYS_SIGNAL_H

#ifdef    HAVE_SYS_STAT_H
#  include <sys/types.h>
#  include <sys/stat.h>
#endif // HAVE_SYS_STAT_H

#ifdef    TIME_WITH_SYS_TIME
#  include <sys/time.h>
#  include <time.h>
#else // !TIME_WITH_SYS_TIME
#  ifdef    HAVE_SYS_TIME_H
#    include <sys/time.h>
#  else // !HAVE_SYS_TIME_H
#    include <time.h>
#  endif // HAVE_SYS_TIME_H
#endif // TIME_WITH_SYS_TIME

#ifdef    HAVE_LIBGEN_H
#  include <libgen.h>
#endif // HAVE_LIBGEN_H
}

#include <assert.h>

#include <algorithm>
#include <string>
using std::string;

#include "i18n.hh"
#include "blackbox.hh"
#include "GCCache.hh"
#include "Screen.hh"
#ifdef ADD_BLOAT
#include "Slit.hh"
#include "Toolbar.hh"
#endif // ADD_BLOAT
#include "Util.hh"
#include "Window.hh"
#include "Workspace.hh"


Blackbox *blackbox;


Blackbox::Blackbox(char **m_argv, char *dpy_name, char */*rc*/)
  : BaseDisplay(m_argv[0], dpy_name) {
  if (! XSupportsLocale())
    fprintf(stderr, "X server does not support locale\n");

  if (XSetLocaleModifiers("") == NULL)
    fprintf(stderr, "cannot set locale modifiers\n");

  ::blackbox = this;
  argv = m_argv;

  no_focus = False;

  resource.auto_raise_delay.tv_sec = resource.auto_raise_delay.tv_usec = 0;

  active_screen = 0;
  focused_window = (BlackboxWindow *) 0;

  load_rc();

  init_icccm();

  cursor.session = XCreateFontCursor(getXDisplay(), XC_left_ptr);
  cursor.move = XCreateFontCursor(getXDisplay(), XC_fleur);
  cursor.ll_angle = XCreateFontCursor(getXDisplay(), XC_ll_angle);
  cursor.lr_angle = XCreateFontCursor(getXDisplay(), XC_lr_angle);

  for (unsigned int i = 0; i < getNumberOfScreens(); i++) {
    BScreen *screen = new BScreen(this, i);

    if (! screen->isScreenManaged()) {
      delete screen;
      continue;
    }

    screenList.push_back(screen);
  }

  if (screenList.empty()) {
    fprintf(stderr,
            i18n(blackboxSet, blackboxNoManagableScreens,
              "Blackbox::Blackbox: no managable screens found, aborting.\n"));
    ::exit(3);
  }

  // set the screen with mouse to the first managed screen
  active_screen = screenList.front();
  setFocusedWindow(0);

  XSynchronize(getXDisplay(), False);
  XSync(getXDisplay(), False);

  reconfigure_wait = False;

  timer = new BTimer(this, this);
  timer->setTimeout(0l);
}


Blackbox::~Blackbox(void) {
  std::for_each(screenList.begin(), screenList.end(), PointerAssassin());

  delete timer;
}


void Blackbox::process_event(XEvent *e) {
  switch (e->type) {
  case ButtonPress: {
    // strip the lock key modifiers
    e->xbutton.state &= ~(NumLockMask | ScrollLockMask | LockMask);

    last_time = e->xbutton.time;

    BlackboxWindow *win = (BlackboxWindow *) 0;
#ifdef ADD_BLOAT
    Slit *slit = (Slit *) 0;
    Toolbar *tbar = (Toolbar *) 0;
#endif // ADD_BLOAT
    BScreen *scrn = (BScreen *) 0;

    if ((win = searchWindow(e->xbutton.window))) {
      win->buttonPressEvent(&e->xbutton);

      /* XXX: is this sane on low colour desktops? */
      if (e->xbutton.button == 1)
        win->installColormap(True);
#ifdef ADD_BLOAT
    } else if ((slit = searchSlit(e->xbutton.window))) {
      slit->buttonPressEvent(&e->xbutton);
    } else if ((tbar = searchToolbar(e->xbutton.window))) {
      tbar->buttonPressEvent(&e->xbutton);
#endif // ADD_BLOAT
    } else if ((scrn = searchScreen(e->xbutton.window))) {
      scrn->buttonPressEvent(&e->xbutton);
      if (active_screen != scrn) {
        active_screen = scrn;
        // first, set no focus window on the old screen
        setFocusedWindow(0);
        // and move focus to this screen
        setFocusedWindow(0);
      }
    }
    break;
  }

  case ButtonRelease: {
    // strip the lock key modifiers
    e->xbutton.state &= ~(NumLockMask | ScrollLockMask | LockMask);

    last_time = e->xbutton.time;

    BlackboxWindow *win = (BlackboxWindow *) 0;
#ifdef ADD_BLOAT
    Toolbar *tbar = (Toolbar *) 0;
#endif // ADD_BLOAT

    if ((win = searchWindow(e->xbutton.window)))
      win->buttonReleaseEvent(&e->xbutton);
#ifdef ADD_BLOAT
    else if ((tbar = searchToolbar(e->xbutton.window)))
      tbar->buttonReleaseEvent(&e->xbutton);
#endif // ADD_BLOAT

    break;
  }

  case ConfigureRequest: {
#ifdef    DEBUG
    fprintf(stderr, "ConfigureRequest\n");
#endif
    BlackboxWindow *win = (BlackboxWindow *) 0;
#ifdef ADD_BLOAT
    Slit *slit = (Slit *) 0;
#endif // ADD_BLOAT

    if ((win = searchWindow(e->xconfigurerequest.window))) {
      win->configureRequestEvent(&e->xconfigurerequest);
#ifdef ADD_BLOAT
    } else if ((slit = searchSlit(e->xconfigurerequest.window))) {
      slit->configureRequestEvent(&e->xconfigurerequest);
#endif // ADD_BLOAT
    } else {
      if (validateWindow(e->xconfigurerequest.window)) {
        XWindowChanges xwc;

        xwc.x = e->xconfigurerequest.x;
        xwc.y = e->xconfigurerequest.y;
        xwc.width = e->xconfigurerequest.width;
        xwc.height = e->xconfigurerequest.height;
        xwc.border_width = e->xconfigurerequest.border_width;
        xwc.sibling = e->xconfigurerequest.above;
        xwc.stack_mode = e->xconfigurerequest.detail;

        XConfigureWindow(getXDisplay(), e->xconfigurerequest.window,
                         e->xconfigurerequest.value_mask, &xwc);
      }
    }

#ifdef    DEBUG
    fprintf(stderr, "ConfigureRequest - done\n");
#endif

    break;
  }

  case ConfigureNotify: {
    BlackboxWindow *win = (BlackboxWindow *) 0;
#ifdef    DEBUG
    fprintf(stderr, "ConfigureNotify\n");
#endif

    if ((win = searchWindow(e->xconfigure.window))) {
      win->configureNotifyEvent(&e->xconfigure);
    }

#ifdef    DEBUG
    fprintf(stderr, "ConfigureNotify - done\n");
#endif

    break;
  }

  case MapRequest: {
#ifdef    DEBUG
    fprintf(stderr, "Blackbox::process_event(): MapRequest for 0x%lx\n",
            e->xmaprequest.window);
#endif // DEBUG

    BlackboxWindow *win = searchWindow(e->xmaprequest.window);

    if (win) {
      bool focus = False;
      if (win->isIconic()) {
        win->deiconify();
        focus = True;
      }

      if (focus && (win->isTransient() || win->getScreen()->doFocusNew()))
        win->setInputFocus();
    } else {
      BScreen *screen = searchScreen(e->xmaprequest.parent);

      if (! screen) {
        /*
          we got a map request for a window who's parent isn't root. this
          can happen in only one circumstance:

            a client window unmapped a managed window, and then remapped it
            somewhere between unmapping the client window and reparenting it
            to root.

          regardless of how it happens, we need to find the screen that
          the window is on
        */
        XWindowAttributes wattrib;
        if (! XGetWindowAttributes(getXDisplay(), e->xmaprequest.window,
                                   &wattrib)) {
          // failed to get the window attributes, perhaps the window has
          // now been destroyed?
          break;
        }

        screen = searchScreen(wattrib.root);
        assert(screen != 0); // this should never happen
      }
      screen->manageWindow(e->xmaprequest.window);
    }

    break;
  }

  case UnmapNotify: {
    BlackboxWindow *win = (BlackboxWindow *) 0;
#ifdef ADD_BLOAT
    Slit *slit = (Slit *) 0;
#endif // ADD_BLOAT

    if ((win = searchWindow(e->xunmap.window))) {
      win->unmapNotifyEvent(&e->xunmap);
#ifdef ADD_BLOAT
    } else if ((slit = searchSlit(e->xunmap.window))) {
      slit->unmapNotifyEvent(&e->xunmap);
#endif // ADD_BLOAT
    }

    break;
  }

  case DestroyNotify: {
    BlackboxWindow *win = (BlackboxWindow *) 0;
#ifdef ADD_BLOAT
    Slit *slit = (Slit *) 0;
#endif // ADD_BLOAT
    BWindowGroup *group = (BWindowGroup *) 0;

    if ((win = searchWindow(e->xdestroywindow.window))) {
      win->destroyNotifyEvent(&e->xdestroywindow);
#ifdef ADD_BLOAT
    } else if ((slit = searchSlit(e->xdestroywindow.window))) {
      slit->removeClient(e->xdestroywindow.window, False);
#endif // ADD_BLOAT
    } else if ((group = searchGroup(e->xdestroywindow.window))) {
      delete group;
    }

    break;
  }

  case ReparentNotify: {
    /*
      this event is quite rare and is usually handled in unmapNotify
      however, if the window is unmapped when the reparent event occurs
      the window manager never sees it because an unmap event is not sent
      to an already unmapped window.
    */
    BlackboxWindow *win = searchWindow(e->xreparent.window);
    if (win) {
      win->reparentNotifyEvent(&e->xreparent);
#ifdef ADD_BLOAT
    } else {
      Slit *slit = searchSlit(e->xreparent.window);
      if (slit && slit->getWindowID() != e->xreparent.parent)
        slit->removeClient(e->xreparent.window, True);
#endif // ADD_BLOAT
    }
    break;
  }

  case MotionNotify: {
    // motion notify compression...
    XEvent realevent;
    unsigned int i = 0;
    while (XCheckTypedWindowEvent(getXDisplay(), e->xmotion.window,
                                  MotionNotify, &realevent)) {
      i++;
    }

    // if we have compressed some motion events, use the last one
    if ( i > 0 )
      e = &realevent;

    // strip the lock key modifiers
    e->xbutton.state &= ~(NumLockMask | ScrollLockMask | LockMask);

    last_time = e->xmotion.time;

    BlackboxWindow *win = (BlackboxWindow *) 0;

    if ((win = searchWindow(e->xmotion.window)))
      win->motionNotifyEvent(&e->xmotion);

    break;
  }

  case PropertyNotify: {
    last_time = e->xproperty.time;

    BlackboxWindow *win = searchWindow(e->xproperty.window);
    if (win)
      win->propertyNotifyEvent(&e->xproperty);
    break;
  }

  case EnterNotify: {
    last_time = e->xcrossing.time;

    //BScreen *screen = (BScreen *) 0;
    BlackboxWindow *win = (BlackboxWindow *) 0;
#ifdef ADD_BLOAT
    Toolbar *tbar = (Toolbar *) 0;
    Slit *slit = (Slit *) 0;
#endif // ADD_BLOAT

    if (e->xcrossing.mode == NotifyGrab) break;

    if ((win = searchWindow(e->xcrossing.window))) {
      if (! no_focus)
        win->enterNotifyEvent(&e->xcrossing);
#ifdef ADD_BLOAT
    } else if ((tbar = searchToolbar(e->xcrossing.window))) {
      tbar->enterNotifyEvent(&e->xcrossing);
    } else if ((slit = searchSlit(e->xcrossing.window))) {
      slit->enterNotifyEvent(&e->xcrossing);
#endif // ADD_BLOAT
    }
    break;
  }

  case LeaveNotify: {
    last_time = e->xcrossing.time;

    BlackboxWindow *win = (BlackboxWindow *) 0;
#ifdef ADD_BLOAT
    Toolbar *tbar = (Toolbar *) 0;
    Slit *slit = (Slit *) 0;
#endif // ADD_BLOAT

    if ((win = searchWindow(e->xcrossing.window)))
      win->leaveNotifyEvent(&e->xcrossing);
#ifdef ADD_BLOAT
    else if ((tbar = searchToolbar(e->xcrossing.window)))
      tbar->leaveNotifyEvent(&e->xcrossing);
    else if ((slit = searchSlit(e->xcrossing.window)))
      slit->leaveNotifyEvent(&e->xcrossing);
#endif // ADD_BLOAT
    break;
  }

  case Expose: {
    // compress expose events
    XEvent realevent;
    unsigned int i = 0;
    int ex1, ey1, ex2, ey2;
    ex1 = e->xexpose.x;
    ey1 = e->xexpose.y;
    ex2 = ex1 + e->xexpose.width - 1;
    ey2 = ey1 + e->xexpose.height - 1;
    while (XCheckTypedWindowEvent(getXDisplay(), e->xexpose.window,
                                  Expose, &realevent)) {
      i++;

      // merge expose area
      ex1 = std::min(realevent.xexpose.x, ex1);
      ey1 = std::min(realevent.xexpose.y, ey1);
      ex2 = std::max(realevent.xexpose.x + realevent.xexpose.width - 1, ex2);
      ey2 = std::max(realevent.xexpose.y + realevent.xexpose.height - 1, ey2);
    }
    if ( i > 0 )
      e = &realevent;

    // use the merged area
    e->xexpose.x = ex1;
    e->xexpose.y = ey1;
    e->xexpose.width = ex2 - ex1 + 1;
    e->xexpose.height = ey2 - ey1 + 1;

    BlackboxWindow *win = (BlackboxWindow *) 0;
#ifdef ADD_BLOAT
    Toolbar *tbar = (Toolbar *) 0;
#endif // ADD_BLOAT

    if ((win = searchWindow(e->xexpose.window)))
      win->exposeEvent(&e->xexpose);
#ifdef ADD_BLOAT
    else if ((tbar = searchToolbar(e->xexpose.window)))
      tbar->exposeEvent(&e->xexpose);
#endif // ADD_BLOAT

    break;
  }

  case KeyPress: {
#ifdef ADD_BLOAT
    Toolbar *tbar = searchToolbar(e->xkey.window);

    if (tbar && tbar->isEditing())
      tbar->keyPressEvent(&e->xkey);
#endif // ADD_BLOAT

    break;
  }

  case ColormapNotify: {
    BScreen *screen = searchScreen(e->xcolormap.window);

    if (screen)
      screen->setRootColormapInstalled((e->xcolormap.state ==
                                        ColormapInstalled) ? True : False);

    break;
  }

  case FocusIn: {
    if (e->xfocus.detail != NotifyNonlinear) {
      /*
        don't process FocusIns when:
        1. the new focus window isn't an ancestor or inferior of the old
        focus window (NotifyNonlinear)
      */
      break;
    }

    BlackboxWindow *win = searchWindow(e->xfocus.window);
    if (win) {
      if (! win->isFocused())
        win->setFocusFlag(True);

      /*
        set the event window to None.  when the FocusOut event handler calls
        this function recursively, it uses this as an indication that focus
        has moved to a known window.
      */
      e->xfocus.window = None;
    }

    break;
  }

  case FocusOut: {
    if (e->xfocus.detail != NotifyNonlinear) {
      /*
        don't process FocusOuts when:
        2. the new focus window isn't an ancestor or inferior of the old
        focus window (NotifyNonlinear)
      */
      break;
    }

    BlackboxWindow *win = searchWindow(e->xfocus.window);
    if (win && win->isFocused()) {
      /*
        before we mark "win" as unfocused, we need to verify that focus is
        going to a known location, is in a known location, or set focus
        to a known location.
      */

      XEvent event;
      // don't check the current focus if FocusOut was generated during a grab
      bool check_focus = (e->xfocus.mode == NotifyNormal);

      /*
        First, check if there is a pending FocusIn event waiting.  if there
        is, process it and determine if focus has moved to another window
        (the FocusIn event handler sets the window in the event
        structure to None to indicate this).
      */
      if (XCheckTypedEvent(getXDisplay(), FocusIn, &event)) {

        process_event(&event);
        if (event.xfocus.window == None) {
          // focus has moved
          check_focus = False;
        }
      }

      if (check_focus) {
        /*
          Second, we query the X server for the current input focus.
          to make sure that we keep a consistent state.
        */
        BlackboxWindow *focus;
        Window w;
        int revert;
        XGetInputFocus(getXDisplay(), &w, &revert);
        focus = searchWindow(w);
        if (focus) {
          /*
            focus got from "win" to "focus" under some very strange
            circumstances, and we need to make sure that the focus indication
            is correct.
          */
          setFocusedWindow(focus);
        } else {
          // we have no idea where focus went... so we set it to somewhere
          setFocusedWindow(0);
        }
      }
    }

    break;
  }

  case ClientMessage: {
    if (e->xclient.format == 32) {
      if (e->xclient.message_type == getWMChangeStateAtom()) {
        BlackboxWindow *win = searchWindow(e->xclient.window);
        if (! win || ! win->validateClient()) return;

        if (e->xclient.data.l[0] == IconicState)
          win->iconify();
        if (e->xclient.data.l[0] == NormalState)
          win->deiconify();
      } else if(e->xclient.message_type == getBlackboxChangeWorkspaceAtom()) {
        BScreen *screen = searchScreen(e->xclient.window);
        unsigned int workspace = e->xclient.data.l[0];
        if (screen && workspace < screen->getWorkspaceCount())
          screen->changeWorkspaceID(workspace);
      } else if (e->xclient.message_type == getBlackboxChangeWindowFocusAtom()) {
        BlackboxWindow *win = searchWindow(e->xclient.window);

        if (win && win->isVisible() && win->setInputFocus())
          win->installColormap(True);
      } else if (e->xclient.message_type == getBlackboxCycleWindowFocusAtom()) {
        BScreen *screen = searchScreen(e->xclient.window);

        if (screen) {
          if (! e->xclient.data.l[0])
            screen->prevFocus();
          else
            screen->nextFocus();
        }
      } else if (e->xclient.message_type == getBlackboxChangeAttributesAtom()) {
        BlackboxWindow *win = searchWindow(e->xclient.window);

        if (win && win->validateClient()) {
          BlackboxHints net;
          net.flags = e->xclient.data.l[0];
          net.attrib = e->xclient.data.l[1];
          net.workspace = e->xclient.data.l[2];
          net.stack = e->xclient.data.l[3];
          net.decoration = e->xclient.data.l[4];

          win->changeBlackboxHints(&net);
        }
      }
    }

    break;
  }

  case NoExpose:
  case MapNotify:
    break; // not handled, just ignore

  default: {
#ifdef    DEBUG
    fprintf (stderr, "default %d %d %d %d %d\n",
             e->type, getShapeEventBase(), getWindowsWMEventBase(),
             WindowsWMControllerNotify, WindowsWMActivationNotify);
#endif
#ifdef    SHAPE
    if (e->type == getShapeEventBase()) {
      XShapeEvent *shape_event = (XShapeEvent *) e;
      BlackboxWindow *win = searchWindow(e->xany.window);

      if (win)
        win->shapeEvent(shape_event);
    }
#endif // SHAPE
    
    if (e->type == getWindowsWMEventBase() + WindowsWMControllerNotify) {
      XWindowsWMNotifyEvent *windows_wm_event = (XWindowsWMNotifyEvent *) e;
      BlackboxWindow *win = searchWindow(e->xany.window);

#ifdef    DEBUG
      fprintf (stderr, "WindowsWMControllerNotify %08x\n", win);
#endif
      if (win)
        win->windowsWMControllerEvent(windows_wm_event);
    }
    if (e->type == getWindowsWMEventBase() + WindowsWMActivationNotify) {
      XWindowsWMNotifyEvent *windows_wm_event = (XWindowsWMNotifyEvent *) e;
      BlackboxWindow *win = searchWindow(e->xany.window);
      //BScreen *screen = searchScreen(e->xclient.window);

      //printf ("WindowsWMActivationNotify\n");
      switch (windows_wm_event->arg) {
      case WindowsWMIsActive:
        //printf ("\tWindowsWMIsAcitve\n");
        break;
      case WindowsWMIsInactive:
        //printf ("\tWindowsWMIsInactive\n");
        if (win)
          {
            //printf ("\tungrab\n");
            //win->ungrabButtons();
            //win->grabButtons();
          }
        setFocusedWindow(0);
        XSetInputFocus(getXDisplay(), None, None, CurrentTime);
        break;
      default:
        fprintf(stderr, "\tUnknown WindowsWMEvent\n");
      }
    }
  }
  } // switch
}


bool Blackbox::handleSignal(int sig) {
  switch (sig) {
  case SIGHUP:
    reconfigure();
    break;

  case SIGUSR1:
    reload_rc();
    break;

  case SIGUSR2:
    //rereadMenu();
    break;

  case SIGPIPE:
  case SIGSEGV:
  case SIGFPE:
  case SIGINT:
  case SIGTERM:
    shutdown();

  default:
    return False;
  }

  return True;
}


void Blackbox::init_icccm(void) {
  xa_wm_colormap_windows =
    XInternAtom(getXDisplay(), "WM_COLORMAP_WINDOWS", False);
  xa_wm_protocols = XInternAtom(getXDisplay(), "WM_PROTOCOLS", False);
  xa_wm_state = XInternAtom(getXDisplay(), "WM_STATE", False);
  xa_wm_change_state = XInternAtom(getXDisplay(), "WM_CHANGE_STATE", False);
  xa_wm_delete_window = XInternAtom(getXDisplay(), "WM_DELETE_WINDOW", False);
  xa_wm_take_focus = XInternAtom(getXDisplay(), "WM_TAKE_FOCUS", False);
  motif_wm_hints = XInternAtom(getXDisplay(), "_MOTIF_WM_HINTS", False);

  blackbox_hints = XInternAtom(getXDisplay(), "_BLACKBOX_HINTS", False);
  blackbox_attributes =
    XInternAtom(getXDisplay(), "_BLACKBOX_ATTRIBUTES", False);
  blackbox_change_attributes =
    XInternAtom(getXDisplay(), "_BLACKBOX_CHANGE_ATTRIBUTES", False);
  blackbox_structure_messages =
    XInternAtom(getXDisplay(), "_BLACKBOX_STRUCTURE_MESSAGES", False);
  blackbox_notify_startup =
    XInternAtom(getXDisplay(), "_BLACKBOX_NOTIFY_STARTUP", False);
  blackbox_notify_window_add =
    XInternAtom(getXDisplay(), "_BLACKBOX_NOTIFY_WINDOW_ADD", False);
  blackbox_notify_window_del =
    XInternAtom(getXDisplay(), "_BLACKBOX_NOTIFY_WINDOW_DEL", False);
  blackbox_notify_current_workspace =
    XInternAtom(getXDisplay(), "_BLACKBOX_NOTIFY_CURRENT_WORKSPACE", False);
  blackbox_notify_workspace_count =
    XInternAtom(getXDisplay(), "_BLACKBOX_NOTIFY_WORKSPACE_COUNT", False);
  blackbox_notify_window_focus =
    XInternAtom(getXDisplay(), "_BLACKBOX_NOTIFY_WINDOW_FOCUS", False);
  blackbox_notify_window_raise =
    XInternAtom(getXDisplay(), "_BLACKBOX_NOTIFY_WINDOW_RAISE", False);
  blackbox_notify_window_lower =
    XInternAtom(getXDisplay(), "_BLACKBOX_NOTIFY_WINDOW_LOWER", False);
  blackbox_change_workspace =
    XInternAtom(getXDisplay(), "_BLACKBOX_CHANGE_WORKSPACE", False);
  blackbox_change_window_focus =
    XInternAtom(getXDisplay(), "_BLACKBOX_CHANGE_WINDOW_FOCUS", False);
  blackbox_cycle_window_focus =
    XInternAtom(getXDisplay(), "_BLACKBOX_CYCLE_WINDOW_FOCUS", False);

  windowswm_raise_on_click =
    XInternAtom(getXDisplay(), WINDOWSWM_RAISE_ON_CLICK, False);
  windowswm_mouse_activate =
    XInternAtom(getXDisplay(), WINDOWSWM_MOUSE_ACTIVATE, False);
  windowswm_client_window =
    XInternAtom(getXDisplay(), WINDOWSWM_CLIENT_WINDOW, False);
  windowswm_native_hwnd =
    XInternAtom(getXDisplay(), WINDOWSWM_NATIVE_HWND, False);

#ifdef    NEWWMSPEC
  net_supported = XInternAtom(getXDisplay(), "_NET_SUPPORTED", False);
  net_client_list = XInternAtom(getXDisplay(), "_NET_CLIENT_LIST", False);
  net_client_list_stacking =
    XInternAtom(getXDisplay(), "_NET_CLIENT_LIST_STACKING", False);
  net_number_of_desktops =
    XInternAtom(getXDisplay(), "_NET_NUMBER_OF_DESKTOPS", False);
  net_desktop_geometry =
    XInternAtom(getXDisplay(), "_NET_DESKTOP_GEOMETRY", False);
  net_desktop_viewport =
    XInternAtom(getXDisplay(), "_NET_DESKTOP_VIEWPORT", False);
  net_current_desktop =
    XInternAtom(getXDisplay(), "_NET_CURRENT_DESKTOP", False);
  net_desktop_names = XInternAtom(getXDisplay(), "_NET_DESKTOP_NAMES", False);
  net_active_window = XInternAtom(getXDisplay(), "_NET_ACTIVE_WINDOW", False);
  net_workarea = XInternAtom(getXDisplay(), "_NET_WORKAREA", False);
  net_supporting_wm_check =
    XInternAtom(getXDisplay(), "_NET_SUPPORTING_WM_CHECK", False);
  net_virtual_roots = XInternAtom(getXDisplay(), "_NET_VIRTUAL_ROOTS", False);
  net_close_window = XInternAtom(getXDisplay(), "_NET_CLOSE_WINDOW", False);
  net_wm_moveresize = XInternAtom(getXDisplay(), "_NET_WM_MOVERESIZE", False);
  net_properties = XInternAtom(getXDisplay(), "_NET_PROPERTIES", False);
  net_wm_name = XInternAtom(getXDisplay(), "_NET_WM_NAME", False);
  net_wm_desktop = XInternAtom(getXDisplay(), "_NET_WM_DESKTOP", False);
  net_wm_window_type =
    XInternAtom(getXDisplay(), "_NET_WM_WINDOW_TYPE", False);
  net_wm_state = XInternAtom(getXDisplay(), "_NET_WM_STATE", False);
  net_wm_strut = XInternAtom(getXDisplay(), "_NET_WM_STRUT", False);
  net_wm_icon_geometry =
    XInternAtom(getXDisplay(), "_NET_WM_ICON_GEOMETRY", False);
  net_wm_icon = XInternAtom(getXDisplay(), "_NET_WM_ICON", False);
  net_wm_pid = XInternAtom(getXDisplay(), "_NET_WM_PID", False);
  net_wm_handled_icons =
    XInternAtom(getXDisplay(), "_NET_WM_HANDLED_ICONS", False);
  net_wm_ping = XInternAtom(getXDisplay(), "_NET_WM_PING", False);
#endif // NEWWMSPEC

#ifdef    HAVE_GETPID
  blackbox_pid = XInternAtom(getXDisplay(), "_BLACKBOX_PID", False);
#endif // HAVE_GETPID
}


bool Blackbox::validateWindow(Window window) {
  XEvent event;
  if (XCheckTypedWindowEvent(getXDisplay(), window, DestroyNotify, &event)) {
    XPutBackEvent(getXDisplay(), &event);

    return False;
  }

  return True;
}


BScreen *Blackbox::searchScreen(Window window) {
  ScreenList::iterator it = screenList.begin();

  for (; it != screenList.end(); ++it) {
    BScreen *s = *it;
    if (s->getRootWindow() == window)
      return s;
  }

  return (BScreen *) 0;
}


BlackboxWindow *Blackbox::searchWindow(Window window) {
  WindowLookup::iterator it = windowSearchList.find(window);
  if (it != windowSearchList.end())
    return it->second;

  return (BlackboxWindow*) 0;
}


BWindowGroup *Blackbox::searchGroup(Window window) {
  GroupLookup::iterator it = groupSearchList.find(window);
  if (it != groupSearchList.end())
    return it->second;

  return (BWindowGroup *) 0;
}

#ifdef ADD_BLOAT
Toolbar *Blackbox::searchToolbar(Window window) {
  ToolbarLookup::iterator it = toolbarSearchList.find(window);
  if (it != toolbarSearchList.end())
    return it->second;

  return (Toolbar*) 0;
}


Slit *Blackbox::searchSlit(Window window) {
  SlitLookup::iterator it = slitSearchList.find(window);
  if (it != slitSearchList.end())
    return it->second;

  return (Slit*) 0;
}
#endif // ADD_BLOAT


void Blackbox::saveWindowSearch(Window window, BlackboxWindow *data) {
  windowSearchList.insert(WindowLookupPair(window, data));
}


void Blackbox::saveGroupSearch(Window window, BWindowGroup *data) {
  groupSearchList.insert(GroupLookupPair(window, data));
}

#ifdef ADD_BLOAT
void Blackbox::saveToolbarSearch(Window window, Toolbar *data) {
  toolbarSearchList.insert(ToolbarLookupPair(window, data));
}


void Blackbox::saveSlitSearch(Window window, Slit *data) {
  slitSearchList.insert(SlitLookupPair(window, data));
}
#endif // ADD_BLOAT


void Blackbox::removeWindowSearch(Window window) {
  windowSearchList.erase(window);
}


void Blackbox::removeGroupSearch(Window window) {
  groupSearchList.erase(window);
}


#ifdef ADD_BLOAT
void Blackbox::removeToolbarSearch(Window window) {
  toolbarSearchList.erase(window);
}


void Blackbox::removeSlitSearch(Window window) {
  slitSearchList.erase(window);
}
#endif // ADD_BLOAT


void Blackbox::restart(const char *prog) {
  shutdown();

  if (prog) {
    putenv(const_cast<char *>(screenList.front()->displayString().c_str()));
    execlp(prog, prog, NULL);
    perror(prog);
  }

  // fall back in case the above execlp doesn't work
  execvp(argv[0], argv);
  string name = basename(argv[0]);
  execvp(name.c_str(), argv);
}


void Blackbox::shutdown(void) {
  BaseDisplay::shutdown();

  XSetInputFocus(getXDisplay(), PointerRoot, None, CurrentTime);

  std::for_each(screenList.begin(), screenList.end(),
                std::mem_fun(&BScreen::shutdown));

  XSync(getXDisplay(), False);

  save_rc();
}


void Blackbox::save_rc(void) {
}


void Blackbox::load_rc(void) {
  resource.colors_per_channel = 4;
  resource.double_click_interval = 250;
  resource.auto_raise_delay.tv_usec = 400;
  resource.auto_raise_delay.tv_sec = resource.auto_raise_delay.tv_usec / 1000;
  resource.auto_raise_delay.tv_usec -=
    (resource.auto_raise_delay.tv_sec * 1000);
  resource.auto_raise_delay.tv_usec *= 1000;
  resource.cache_life = 5l;
  resource.cache_life *= 60000;
  resource.cache_max = 200;
}


void Blackbox::load_rc(BScreen *screen) {
  screen->saveFullMax(False);
  //screen->saveFocusNew(False);
  screen->saveFocusNew(True);
  screen->saveFocusLast(False);
  screen->saveAllowScrollLock(False);
  screen->saveRowPlacementDirection(BScreen::LeftRight);
  screen->saveColPlacementDirection(BScreen::TopBottom);
  screen->saveWorkspaces(1);
  screen->saveSloppyFocus(True);
  screen->saveAutoRaise(False);
  screen->saveClickRaise(False);
  screen->savePlacementPolicy(BScreen::RowSmartPlacement);

  screen->saveEdgeSnapThreshold(0);
  screen->saveImageDither(True);
  screen->saveOpaqueMove(False);
}


void Blackbox::reload_rc(void) {
  load_rc();
  reconfigure();
}


void Blackbox::reconfigure(void) {
  reconfigure_wait = True;

  if (! timer->isTiming()) timer->start();
}


void Blackbox::real_reconfigure(void) {
  gcCache()->purge();

  std::for_each(screenList.begin(), screenList.end(),
                std::mem_fun(&BScreen::reconfigure));
}


void Blackbox::timeout(void) {
  if (reconfigure_wait)
    real_reconfigure();

  reconfigure_wait = False;
}


void Blackbox::setFocusedWindow(BlackboxWindow *win) {
  if (focused_window && focused_window == win) // nothing to do
    return;

  BScreen *old_screen = 0;

  if (focused_window) {
    focused_window->setFocusFlag(False);
    old_screen = focused_window->getScreen();
  }

  if (win && ! win->isIconic()) {
    // the active screen is the one with the last focused window...
    // this will keep focus on this screen no matter where the mouse goes,
    // so multihead keybindings will continue to work on that screen until the
    // user focuses a window on a different screen.
    active_screen = win->getScreen();
    focused_window = win;
  } else {
    focused_window = 0;
#ifdef ADD_BLOAT
    if (! old_screen) {
      if (active_screen) {
        // set input focus to the toolbar of the screen with mouse
        XSetInputFocus(getXDisplay(),
                       active_screen->getToolbar()->getWindowID(),
                       RevertToPointerRoot, CurrentTime);
      } else {
        // set input focus to the toolbar of the first managed screen
        XSetInputFocus(getXDisplay(),
                       screenList.front()->getToolbar()->getWindowID(),
                       RevertToPointerRoot, CurrentTime);
      }
    } else {
      // set input focus to the toolbar of the last screen
      XSetInputFocus(getXDisplay(), old_screen->getToolbar()->getWindowID(),
                     RevertToPointerRoot, CurrentTime);
    }
#endif // ADD_BLOAT
  }

#ifdef ADD_BLOAT
  if (active_screen && active_screen->isScreenManaged()) {
    active_screen->getToolbar()->redrawWindowLabel(True);
    active_screen->updateNetizenWindowFocus();
  }

  if (old_screen && old_screen != active_screen) {
    old_screen->getToolbar()->redrawWindowLabel(True);
    old_screen->updateNetizenWindowFocus();
  }
#endif // ADD_BLOAT
}

#ifdef ENABLE_KEYBINDINGS
void Blackbox::setKeys() {
    if (chpid != 0){
        kill(chpid,SIGTERM);
        chpid = 0;
    }
    
    if (enableKeyBindings()) { 
      chpid = fork();
		
      if (chpid < 0){
          //cerr << "Error: Can't Fork Keybindings" << endl;
      }
	  
      if (chpid == 0){
	    extern char **environ;
 
	    char *args[]= {"sh", "-c", (char*)getkeycmd(), 0};
	    execve("/bin/sh", args, environ);
	    exit(0);
       }
    } 
}
#endif // ENABLE_KEYBINDINGS
