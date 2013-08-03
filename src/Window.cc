// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Window.cc for Blackbox - an X11 Window manager
// Copyright (c) 2003 Kensuke Matsuzaki <zakki@peppermint.jp>
// Copyright (c) 2001 - 2002 Sean 'Shaleh' Perry <shaleh at debian.org>
// Copyright (c) 1997 - 2000, 2002 Brad Hughes <bhughes at trolltech.com>
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
#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <X11/extensions/windowswm.h>
#include <X11/extensions/windowswmstr.h>

#ifdef HAVE_STRING_H
#  include <string.h>
#endif // HAVE_STRING_H

#if defined(DEBUG)
#  ifdef    HAVE_STDIO_H
#    include <stdio.h>
#  endif // HAVE_STDIO_H
#endif // DEBUG

#ifdef HAVE_STDLIB_H
   #include <stdlib.h>
#endif // HAVE_STDLIB_H
}

#include <assert.h>

#include "i18n.hh"
#include "blackbox.hh"
#include "GCCache.hh"
#include "Screen.hh"
#include "Util.hh"
#include "Window.hh"
#include "Workspace.hh"

/* Fixups to prevent collisions between Windows and X headers */
#undef MINSHORT
#undef MAXSHORT
#define INT32 WIN_INT32
#define INT64 WIN_INT64
#undef LONG64

/* Flags for Windows header options */
#define NONAMELESSUNION
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define _NO_BOOL_TYPEDEF
#include <windows.h>


/*
 * Initializes the class with default values/the window's set initial values.
 */
BlackboxWindow::BlackboxWindow(Blackbox *b, Window w, BScreen *s) {
  // fprintf(stderr, "BlackboxWindow size: %d bytes\n",
  // sizeof(BlackboxWindow));

#if defined(DEBUG)
  fprintf(stderr, "BlackboxWindow::BlackboxWindow(): creating 0x%lx\n", w);
#endif // DEBUG

  /*
    set timer to zero... it is initialized properly later, so we check
    if timer is zero in the destructor, and assume that the window is not
    fully constructed if timer is zero...
  */
  timer = 0;
  blackbox = b;
  client.window = w;
  screen = s;

  if (! validateClient()) {
    delete this;
    return;
  }

  // fetch client size and placement
  XWindowAttributes wattrib;
  if (! XGetWindowAttributes(blackbox->getXDisplay(),
                             client.window, &wattrib) ||
      ! wattrib.screen || wattrib.override_redirect) {
#if defined(DEBUG)
    fprintf(stderr,
            "BlackboxWindow::BlackboxWindow(): XGetWindowAttributes failed\n");
#endif // DEBUG

    delete this;
    return;
  }

  // set the eventmask early in the game so that we make sure we get
  // all the events we are interested in
  XSetWindowAttributes attrib_set;
  attrib_set.event_mask = PropertyChangeMask | FocusChangeMask |
                          StructureNotifyMask;
  attrib_set.do_not_propagate_mask = ButtonPressMask | ButtonReleaseMask |
                                     ButtonMotionMask;
  XChangeWindowAttributes(blackbox->getXDisplay(), client.window,
                          CWEventMask|CWDontPropagate, &attrib_set);

  flags.moving = flags.resizing = flags.visible =
    flags.iconic = flags.focused = flags.modal =
    flags.send_focus_message = flags.shaped = False;
  flags.maximized = 0;

  blackbox_attrib.workspace = window_number = BSENTINEL;

  blackbox_attrib.flags = blackbox_attrib.attrib = blackbox_attrib.stack
    = blackbox_attrib.decoration = 0l;
  blackbox_attrib.premax_x = blackbox_attrib.premax_y = 0;
  blackbox_attrib.premax_w = blackbox_attrib.premax_h = 0;

  window_in_taskbar = None;

  decorations = Decor_Titlebar | Decor_Border | Decor_Handle |
                Decor_Iconify | Decor_Maximize;
  functions = Func_Resize | Func_Move | Func_Iconify | Func_Maximize;
  frame_style = WindowsWMFrameStylePopup;
  frame_style_ex = WindowsWMFrameStyleExAppWindow;

  client.normal_hint_flags = 0;
  client.window_group = None;
  client.transient_for = 0;

  current_state = NormalState;

  /*
    set the initial size and location of client window (relative to the
    _root window_). This position is the reference point used with the
    window's gravity to find the window's initial position.
  */
#if defined(DEBUG)
  fprintf(stderr, "BlackboxWindow::BlackboxWindow(): wattrib %d %d %d %d\n",
          wattrib.x, wattrib.y, wattrib.width, wattrib.height, wattrib.border_width);
#endif
  client.rect.setRect(wattrib.x, wattrib.y, wattrib.width, wattrib.height);
  client.old_bw = wattrib.border_width;

  lastButtonPressTime = 0;

  timer = new BTimer(blackbox, this);
  timer->setTimeout(blackbox->getAutoRaiseDelay());

  // get size, aspect, minimum/maximum size and other hints set by the
  // client

  /*
  if (! getBlackboxHints())
    getMWMHints();

  getWMProtocols();
  getWMHints();
  getWMNormalHints();
  getWMClass();
  */

  window_in_taskbar = createToplevelWindow();
  blackbox->saveWindowSearch(window_in_taskbar, this);
  XSelectInput(blackbox->getXDisplay(), window_in_taskbar,
               StructureNotifyMask);

  //FIXME:
  if (! getBlackboxHints())
    getMWMHints();

  getWMProtocols();
  getWMHints();
  getWMNormalHints();
  getWMClass();

  /*XChangeProperty(blackbox->getXDisplay(), frame.window,
                  blackbox->getWindowsWMClientWindow(),
                  XA_INTEGER, 32,
                  PropModeReplace, (unsigned char *) &client.window, 1);*/

  // determine if this is a transient window
  getTransientInfo();

  // adjust the window decorations based on transience and window sizes

  if (isTransient()) {
    decorations &= ~(Decor_Maximize | Decor_Handle);
    functions &= ~Func_Maximize;
  }

  if ((client.normal_hint_flags & PMinSize) &&
      (client.normal_hint_flags & PMaxSize) &&
      client.max_width <= client.min_width &&
      client.max_height <= client.min_height) {
    decorations &= ~(Decor_Maximize | Decor_Handle);
    functions &= ~(Func_Resize | Func_Maximize);
  }

  // now that we know what decorations are required, create them

  if (decorations & Decor_Titlebar)
    createTitlebar();

  if (decorations & Decor_Handle)
    createHandle();

  // apply the size and gravity hint to the frame

  upsize();

  bool place_window = True;
  if (blackbox->isStartup() || isTransient() ||
      client.normal_hint_flags & (PPosition|USPosition)) {
    applyGravity(frame.rectFrame);

    if (blackbox->isStartup() || client.rect.intersects(screen->getRect()))
      place_window = False;
  }

  /*
    the server needs to be grabbed here to prevent client's from sending
    events while we are in the process of configuring their window.
    We hold the grab until after we are done moving the window around.
  */

  XGrabServer(blackbox->getXDisplay());

  associateClientWindow();

  blackbox->saveWindowSearch(client.window, this);

  if (blackbox_attrib.workspace >= screen->getWorkspaceCount())
    screen->getCurrentWorkspace()->addWindow(this, place_window);
  else
    screen->getWorkspace(blackbox_attrib.workspace)->
      addWindow(this, place_window);

  if (! place_window) {
    // don't need to call configure if we are letting the workspace
    // place the window
    configure(frame.rectFrame.x(), frame.rectFrame.y(),
              frame.rectFrame.width(), frame.rectFrame.height());
  }

  positionWindows();

  XUngrabServer(blackbox->getXDisplay());

#ifdef    SHAPE
  if (blackbox->hasShapeExtensions() && flags.shaped)
    configureShape();
#endif // SHAPE

  // now that we know where to put the window and what it should look like
  // we apply the decorations
  decorate();

  grabButtons();

  //XMapSubwindows(blackbox->getXDisplay(), frame.window);

  // this ensures the title, buttons, and other decor are properly displayed
  redrawWindowFrame();

  // preserve the window's initial state on first map, and its current state
  // across a restart
  unsigned long initial_state = current_state;
  if (! getState())
    current_state = initial_state;

  if (flags.maximized && (functions & Func_Maximize))
    remaximize();

#if defined(DEBUG)
  fprintf(stderr, "BlackboxWindow::BlackboxWindow() - done\n");
#endif
}


BlackboxWindow::~BlackboxWindow(void) {
#if defined(DEBUG)
  fprintf(stderr, "BlackboxWindow::~BlackboxWindow: destroying 0x%lx\n",
          window_in_taskbar);
#endif // DEBUG

  if (! timer) // window not managed...
    return;

  if (flags.moving || flags.resizing) {
    XUngrabPointer(blackbox->getXDisplay(), CurrentTime);
  }

  delete timer;

  if (client.window_group) {
    BWindowGroup *group = blackbox->searchGroup(client.window_group);
    if (group) group->removeWindow(this);
  }

  // remove ourselves from our transient_for
  if (isTransient()) {
    if (client.transient_for != (BlackboxWindow *) ~0ul)
      client.transient_for->client.transientList.remove(this);

    client.transient_for = (BlackboxWindow*) 0;
  }

  if (client.transientList.size() > 0) {
    // reset transient_for for all transients
    BlackboxWindowList::iterator it, end = client.transientList.end();
    for (it = client.transientList.begin(); it != end; ++it)
      (*it)->client.transient_for = (BlackboxWindow*) 0;
  }

  if (window_in_taskbar) {
    XDestroyWindow(blackbox->getXDisplay(), window_in_taskbar);
  }

  XDeleteProperty(blackbox->getXDisplay(), client.window,
                  blackbox->getWindowsWMRaiseOnClick());
  XDeleteProperty(blackbox->getXDisplay(), client.window,
                  blackbox->getWindowsWMMouseActivate());

  blackbox->removeWindowSearch(window_in_taskbar);
  blackbox->removeWindowSearch(client.window);
}


/*
 * Creates a new top level window, with a given location, size, and border
 * width.
 * Returns: the newly created window
 */
Window BlackboxWindow::createToplevelWindow(void) {
  XSetWindowAttributes attrib_create;
  unsigned long create_mask = CWBackPixmap | CWBorderPixel | CWColormap |
                              CWOverrideRedirect | CWEventMask;

  attrib_create.background_pixmap = None;
  attrib_create.colormap = screen->getColormap();
  attrib_create.override_redirect = True;
  attrib_create.event_mask = EnterWindowMask | LeaveWindowMask;

  return XCreateWindow(blackbox->getXDisplay(), screen->getRootWindow(),
                       0, 0, 1, 1, 0, screen->getDepth(),
                       InputOutput, screen->getVisual(), create_mask,
                       &attrib_create);
}


/*
 * Creates a child window, and optionally associates a given cursor with
 * the new window.
 */
Window BlackboxWindow::createChildWindow(Window parent,
                                         unsigned long event_mask,
                                         Cursor cursor) {
  XSetWindowAttributes attrib_create;
  unsigned long create_mask = CWBackPixmap | CWBorderPixel |
                              CWEventMask;

  attrib_create.background_pixmap = None;
  attrib_create.event_mask = event_mask;

  if (cursor) {
    create_mask |= CWCursor;
    attrib_create.cursor = cursor;
  }

  return XCreateWindow(blackbox->getXDisplay(), parent, 0, 0, 1, 1, 0,
                       screen->getDepth(), InputOutput, screen->getVisual(),
                       create_mask, &attrib_create);
}


void BlackboxWindow::associateClientWindow(void) {
  XSetWindowBorderWidth(blackbox->getXDisplay(), client.window, 0);
  getWMName();
  getWMIconName();
  getWMClass();

#if defined(DEBUG)
  fprintf(stderr, "XWindowsWMFrameSetTitle %s\n", client.title.c_str());
#endif

  XWindowsWMFrameSetTitle(blackbox->getXDisplay(), 0, client.window,
                          client.title.length(), client.title.c_str());

  XChangeSaveSet(blackbox->getXDisplay(), client.window, SetModeInsert);

  /*XSelectInput(blackbox->getXDisplay(), frame.window,
    StructureNotifyMask|SubstructureRedirectMask);*/

  /*
    note we used to grab around this call to XReparentWindow however the
    server is now grabbed before this method is called
  */
  unsigned long event_mask = PropertyChangeMask | FocusChangeMask |
                             StructureNotifyMask;
  XSelectInput(blackbox->getXDisplay(), client.window,
               event_mask & ~StructureNotifyMask);
  //XReparentWindow(blackbox->getXDisplay(), client.window, frame.window, 0, 0);
  XSelectInput(blackbox->getXDisplay(), client.window, event_mask);

  //  XMapSubwindows(blackbox->getXDisplay(), frame.window);
  //XMapWindow(blackbox->getXDisplay(), client.window);

#ifdef    SHAPE
  if (blackbox->hasShapeExtensions()) {
    XShapeSelectInput(blackbox->getXDisplay(), client.window,
                      ShapeNotifyMask);

    Bool shaped = False;
    int foo;
    unsigned int ufoo;

    XShapeQueryExtents(blackbox->getXDisplay(), client.window, &shaped,
                       &foo, &foo, &ufoo, &ufoo, &foo, &foo, &foo,
                       &ufoo, &ufoo);
    flags.shaped = shaped;
  }
#endif // SHAPE
}


void BlackboxWindow::decorate(void) {
  if (decorations & Decor_Titlebar) {
    frame_style |= WindowsWMFrameStyleCaption | WindowsWMFrameStyleSysMenu;
    decorateLabel();
  } else {
    frame_style &= ~(WindowsWMFrameStyleCaption | WindowsWMFrameStyleSysMenu);
  }

  if (decorations & Decor_Border) {
    frame_style |= WindowsWMFrameStyleBorder;
    blackbox_attrib.flags |= AttribDecoration;
    blackbox_attrib.decoration = DecorNormal;
  } else {
    frame_style &= ~WindowsWMFrameStyleBorder;
    blackbox_attrib.flags |= AttribDecoration;
    blackbox_attrib.decoration = DecorNone;
  }

  if (decorations & Decor_Handle) {
    frame_style |= WindowsWMFrameStyleSizeBox;
  } else {
    frame_style &= ~WindowsWMFrameStyleSizeBox;
  }

#if 0
  short fx, fy, fw, fh;
  XWindowsWMFrameGetRect(blackbox->getXDisplay(),
                         frame_style, frame_style_ex, 0,
                         client.rect.x(), client.rect.y(),
                         client.rect.width(), client.rect.height(),
                         &fx, &fy, &fw, &fh);

#if defined(DEBUG)
  fprintf(stderr, "XWindowsWMFrameGetRect %d %d %d %d - %d %d %d %d\n",
          frame.rect.x(), frame.rect.y(),
          frame.rect.width(), frame.rect.height(),
          fx, fy, fw, fh);
#endif

  if (fx < 0 || fy < 0)
    frame.rect.setPos(frame.rect.x()*2 - fx, frame.rect.y()*2 - fy);
#endif

  XWindowsWMFrameDraw(blackbox->getXDisplay(), 0, client.window,
                      frame_style,
                      frame_style_ex,
                      client.rect.x(), client.rect.y(),
                      client.rect.width(), client.rect.height());

  //  XSetWindowBorder(blackbox->getXDisplay(), frame.window,
  //                   screen->getBorderColor()->pixel());

  int prop = 1;
  XChangeProperty(blackbox->getXDisplay(), client.window,
                  blackbox->getWindowsWMRaiseOnClick(),
                  XA_INTEGER, 32,
                  PropModeReplace, (unsigned char *) &prop, 1);
  XChangeProperty(blackbox->getXDisplay(), client.window,
                  blackbox->getWindowsWMMouseActivate(),
                  XA_INTEGER, 32,
                  PropModeReplace, (unsigned char *) &prop, 1);
}


void BlackboxWindow::decorateLabel(void) {
}


void BlackboxWindow::createHandle(void) {
}


void BlackboxWindow::destroyHandle(void) {
}


void BlackboxWindow::createTitlebar(void) {
/*  if (decorations & Decor_Iconify) createIconifyButton();
  if (decorations & Decor_Maximize) createMaximizeButton();
  if (decorations & Decor_Close) createCloseButton();*/
  if (decorations & Decor_Iconify) {
    frame_style |= WindowsWMFrameStyleMinimizeBox;
  } else {
    frame_style &= ~WindowsWMFrameStyleMinimizeBox;
  }
  if (decorations & Decor_Maximize) {
    frame_style |= WindowsWMFrameStyleMaximizeBox;
  } else {
    frame_style &= ~WindowsWMFrameStyleMaximizeBox;
  }
  //FIXME: how?
  if (decorations & Decor_Close) {
  } else {
  }
  XWindowsWMFrameDraw(blackbox->getXDisplay(), 0, client.window,
                      frame_style,
                      frame_style_ex,
                      client.rect.x(), client.rect.y(),
                      client.rect.width(), client.rect.height());
}


void BlackboxWindow::destroyTitlebar(void) {
}


void BlackboxWindow::createCloseButton(void) {
}


void BlackboxWindow::destroyCloseButton(void) {
}


void BlackboxWindow::createIconifyButton(void) {
}


void BlackboxWindow::destroyIconifyButton(void) {
}


void BlackboxWindow::createMaximizeButton(void) {
}


void BlackboxWindow::destroyMaximizeButton(void) {
}


void BlackboxWindow::positionButtons(bool /*redecorate_label*/) {
}


void BlackboxWindow::reconfigure(void) {
  restoreGravity(client.rect);
  upsize();
  applyGravity(frame.rectFrame);
  positionWindows();
  decorate();
  redrawWindowFrame();

  ungrabButtons();
  grabButtons();
}


void BlackboxWindow::grabButtons(void) {
  if (! screen->isSloppyFocus() || screen->doClickRaise())
    // grab button 1 for changing focus/raising
    blackbox->grabButton(Button1, 0, client.window, True, ButtonPressMask,
                         GrabModeSync, GrabModeSync, client.window, None,
                         screen->allowScrollLock());

  if (functions & Func_Move)
    blackbox->grabButton(Button1, Mod1Mask, client.window, True,
                         ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
                         GrabModeAsync, client.window,
                         blackbox->getMoveCursor(),
                         screen->allowScrollLock());
  if (functions & Func_Resize)
    blackbox->grabButton(Button3, Mod1Mask, client.window, True,
                         ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
                         GrabModeAsync, client.window,
                         blackbox->getLowerRightAngleCursor(),
                         screen->allowScrollLock());
  // alt+middle lowers the window
  blackbox->grabButton(Button2, Mod1Mask, client.window, True,
                       ButtonReleaseMask, GrabModeAsync, GrabModeAsync,
                       client.window, None, screen->allowScrollLock());
}


void BlackboxWindow::ungrabButtons(void) {
  blackbox->ungrabButton(Button1, 0, client.window);
  blackbox->ungrabButton(Button1, Mod1Mask, client.window);
  blackbox->ungrabButton(Button2, Mod1Mask, client.window);
  blackbox->ungrabButton(Button3, Mod1Mask, client.window);
}


void BlackboxWindow::positionWindows(void) {
#if defined(DEBUG)
  fprintf(stderr, "positionWindows frame:%d %d %d %d - frame in:%d %d %d %d\n"
         "\tclient:%d %d %d %d\n",
         frame.rectFrame.left(),
         frame.rectFrame.top(),
         frame.rectFrame.width(),
         frame.rectFrame.height(),
         frame.rectFrame.x() + frame.margin.left,
         frame.rectFrame.y() + frame.margin.top,
         frame.rectFrame.width()
         - (frame.margin.right + frame.margin.left),
         frame.rectFrame.height()
         - (frame.margin.top + frame.margin.bottom),
         client.rect.left(),
         client.rect.top(),
         client.rect.width(),
         client.rect.height());
#endif

  client.rect.setPos(frame.rectFrame.x() + frame.margin.left,
                     frame.rectFrame.y() + frame.margin.top);
  XMoveResizeWindow(blackbox->getXDisplay(), client.window,
                    client.rect.x(),
                    client.rect.y(),
                    client.rect.width(),
                    client.rect.height());
  //  XSetWindowBorderWidth(blackbox->getXDisplay(), frame.window, 0);
  //  XMoveResizeWindow(blackbox->getXDisplay(), client.window,
  //                    0, 0, client.rect.width(), client.rect.height());
  // ensure client.rect contains the real location

  if (decorations & Decor_Titlebar) {
    //WindowsWM ext
  } else {
  }
  if (decorations & Decor_Handle) {
    //WindowsWM ext
  } else {
  }
  XSync(blackbox->getXDisplay(), False);
#if 0
  fprintf(stderr, "BlackboxWindow::positionWindows - \n"
          "\t%d %d %d %d\n",
          blackbox_attrib.premax_x, blackbox_attrib.premax_y,
          blackbox_attrib.premax_w, blackbox_attrib.premax_h);
  HWND hWnd = (HWND)getHWnd(client.window);
  if (hWnd && blackbox_attrib.premax_w != 0 && blackbox_attrib.premax_h != 0) {
    WINDOWPLACEMENT wndpl = {sizeof(WINDOWPLACEMENT)};
    if (GetWindowPlacement(hWnd, &wndpl)) {
      SetRect (&wndpl.rcNormalPosition,
               blackbox_attrib.premax_x, blackbox_attrib.premax_y,
               blackbox_attrib.premax_x + blackbox_attrib.premax_w,
               blackbox_attrib.premax_y + blackbox_attrib.premax_h);
      
      if (!SetWindowPlacement(hWnd, &wndpl)) {
        fprintf(stderr, "SetWindowPlacement failed\n");
      }
    } else {
      fprintf(stderr, "GetWindowPlacement failed\n");
    }
    //ShowWindow (hWnd, SW_MINIMIZE);
  }
#endif
}


void BlackboxWindow::getWMName(void) {
  XTextProperty text_prop;

  std::string name;

  if (XGetWMName(blackbox->getXDisplay(), client.window, &text_prop)) {
    name = textPropertyToString(blackbox->getXDisplay(), text_prop);
    XFree((char *) text_prop.value);
  }
  if (! name.empty())
    client.title = name;
  else
    client.title = i18n(WindowSet, WindowUnnamed, "Unnamed");

#if defined(DEBUG_WITH_ID)
  // the 16 is the 8 chars of the debug text plus the number
  char *tmp = new char[client.title.length() + 16];
  sprintf(tmp, "%s; id: 0x%lx", client.title.c_str(), client.window);
  client.title = tmp;
  delete tmp;
#endif
}


void BlackboxWindow::getWMIconName(void) {
  XTextProperty text_prop;

  std::string name;

  if (XGetWMIconName(blackbox->getXDisplay(), client.window, &text_prop)) {
    name = textPropertyToString(blackbox->getXDisplay(), text_prop);
    XFree((char *) text_prop.value);
  }
  if (! name.empty())
    client.icon_title = name;
  else
    client.icon_title = client.title;
}


/*
 * Retrieve which WM Protocols are supported by the client window.
 * If the WM_DELETE_WINDOW protocol is supported, add the close button to the
 * window's decorations and allow the close behavior.
 * If the WM_TAKE_FOCUS protocol is supported, save a value that indicates
 * this.
 */
void BlackboxWindow::getWMProtocols(void) {
  Atom *proto;
  int num_return = 0;

  if (XGetWMProtocols(blackbox->getXDisplay(), client.window,
                      &proto, &num_return)) {
    for (int i = 0; i < num_return; ++i) {
      if (proto[i] == blackbox->getWMDeleteAtom()) {
        decorations |= Decor_Close;
        functions |= Func_Close;
      } else if (proto[i] == blackbox->getWMTakeFocusAtom()) {
        flags.send_focus_message = True;
      } else if (proto[i] == blackbox->getBlackboxStructureMessagesAtom()) {
        screen->addNetizen(new Netizen(screen, client.window));
      }
    }

    XFree(proto);
  }
}


/*
 * Gets the value of the WM_HINTS property.
 * If the property is not set, then use a set of default values.
 */
void BlackboxWindow::getWMHints(void) {
  focus_mode = F_Passive;

  // remove from current window group
  if (client.window_group) {
    BWindowGroup *group = blackbox->searchGroup(client.window_group);
    if (group) group->removeWindow(this);
  }
  client.window_group = None;

  XWMHints *wmhint = XGetWMHints(blackbox->getXDisplay(), client.window);
  if (! wmhint)
    return;

  if (wmhint->flags & InputHint) {
    if (wmhint->input == True) {
      if (flags.send_focus_message)
        focus_mode = F_LocallyActive;
    } else {
      if (flags.send_focus_message)
        focus_mode = F_GloballyActive;
      else
        focus_mode = F_NoInput;
    }
  }

  if (wmhint->flags & StateHint)
    current_state = wmhint->initial_state;

  if (wmhint->flags & WindowGroupHint) {
    client.window_group = wmhint->window_group;

    // add window to the appropriate group
    BWindowGroup *group = blackbox->searchGroup(client.window_group);
    if (! group) { // no group found, create it!
      new BWindowGroup(blackbox, client.window_group);
      group = blackbox->searchGroup(client.window_group);
    }
    if (group)
      group->addWindow(this);
  }

  XSetWMHints(blackbox->getXDisplay(), window_in_taskbar, wmhint);

  //Redraw Icon
  XWindowsWMFrameDraw(blackbox->getXDisplay(), 0, client.window,
                      frame_style,
                      frame_style_ex,
                      client.rect.x(), client.rect.y(),
                      client.rect.width(), client.rect.height());

  XFree(wmhint);
}


/*
 * Gets the value of the WM_NORMAL_HINTS property.
 * If the property is not set, then use a set of default values.
 */
void BlackboxWindow::getWMNormalHints(void) {
  long icccm_mask;
  XSizeHints sizehint;

  client.min_width = client.min_height =
    client.width_inc = client.height_inc = 1;
  client.base_width = client.base_height = 0;
  client.win_gravity = NorthWestGravity;
#if 0
  client.min_aspect_x = client.min_aspect_y =
    client.max_aspect_x = client.max_aspect_y = 1;
#endif

  /*
    use the full screen, not the strut modified size. otherwise when the
    availableArea changes max_width/height will be incorrect and lead to odd
    rendering bugs.
  */
  const Rect& screen_area = screen->getRect();
  client.max_width = screen_area.width();
  client.max_height = screen_area.height();

  if (! XGetWMNormalHints(blackbox->getXDisplay(), client.window,
                          &sizehint, &icccm_mask))
    return;

  client.normal_hint_flags = sizehint.flags;

  if (sizehint.flags & PMinSize) {
    if (sizehint.min_width >= 0)
      client.min_width = sizehint.min_width;
    if (sizehint.min_height >= 0)
      client.min_height = sizehint.min_height;
  }

  if (sizehint.flags & PMaxSize) {
    if (sizehint.max_width > static_cast<signed>(client.min_width))
      client.max_width = sizehint.max_width;
    else
      client.max_width = client.min_width;

    if (sizehint.max_height > static_cast<signed>(client.min_height))
      client.max_height = sizehint.max_height;
    else
      client.max_height = client.min_height;
  }

  if (sizehint.flags & PResizeInc) {
    client.width_inc = sizehint.width_inc;
    client.height_inc = sizehint.height_inc;
  }

#if 0 // we do not support this at the moment
  if (sizehint.flags & PAspect) {
    client.min_aspect_x = sizehint.min_aspect.x;
    client.min_aspect_y = sizehint.min_aspect.y;
    client.max_aspect_x = sizehint.max_aspect.x;
    client.max_aspect_y = sizehint.max_aspect.y;
  }
#endif

  if (sizehint.flags & PBaseSize) {
    client.base_width = sizehint.base_width;
    client.base_height = sizehint.base_height;
  }

  if (sizehint.flags & PWinGravity)
    client.win_gravity = sizehint.win_gravity;

  //XSetWMNormalHints(blackbox->getXDisplay(), frame.window, &sizehint);

#if defined(DEBUG)
  fprintf(stderr, "XSetWMNormalHints\n");
#endif
}


/*
 * Gets the MWM hints for the class' contained window.
 * This is used while initializing the window to its first state, and not
 * thereafter.
 * Returns: true if the MWM hints are successfully retreived and applied;
 * false if they are not.
 */
void BlackboxWindow::getMWMHints(void) {
  int format;
  Atom atom_return;
  unsigned long num, len;
  MwmHints *mwm_hint = 0;

  int ret = XGetWindowProperty(blackbox->getXDisplay(), client.window,
                               blackbox->getMotifWMHintsAtom(), 0,
                               PropMwmHintsElements, False,
                               blackbox->getMotifWMHintsAtom(), &atom_return,
                               &format, &num, &len,
                               (unsigned char **) &mwm_hint);

  if (ret != Success || ! mwm_hint || num != PropMwmHintsElements)
    return;

  if (mwm_hint->flags & MwmHintsDecorations) {
    if (mwm_hint->decorations & MwmDecorAll) {
      decorations = Decor_Titlebar | Decor_Handle | Decor_Border |
                    Decor_Iconify | Decor_Maximize | Decor_Close;
    } else {
      decorations = 0;

      if (mwm_hint->decorations & MwmDecorBorder)
        decorations |= Decor_Border;
      if (mwm_hint->decorations & MwmDecorHandle)
        decorations |= Decor_Handle;
      if (mwm_hint->decorations & MwmDecorTitle)
        decorations |= Decor_Titlebar;
      if (mwm_hint->decorations & MwmDecorIconify)
        decorations |= Decor_Iconify;
      if (mwm_hint->decorations & MwmDecorMaximize)
        decorations |= Decor_Maximize;
    }
  }

  if (mwm_hint->flags & MwmHintsFunctions) {
    if (mwm_hint->functions & MwmFuncAll) {
      functions = Func_Resize | Func_Move | Func_Iconify | Func_Maximize |
                  Func_Close;
    } else {
      functions = 0;

      if (mwm_hint->functions & MwmFuncResize)
        functions |= Func_Resize;
      if (mwm_hint->functions & MwmFuncMove)
        functions |= Func_Move;
      if (mwm_hint->functions & MwmFuncIconify)
        functions |= Func_Iconify;
      if (mwm_hint->functions & MwmFuncMaximize)
        functions |= Func_Maximize;
      if (mwm_hint->functions & MwmFuncClose)
        functions |= Func_Close;
    }
  }
  XFree(mwm_hint);
}


/*
 * Gets the value of the WM_CLASS property.
 * If the property is not set, then use a set of default values.
 */
void BlackboxWindow::getWMClass(void) {
  XClassHint classhints;

  //FIXME: move to extension?
  if (XGetClassHint(blackbox->getXDisplay(), client.window, &classhints) == 0)
    return;

  //XSetClassHint(blackbox->getXDisplay(), frame.window, &classhints);
  XFree(classhints.res_name);
  XFree(classhints.res_class);
}


/*
 * Gets the blackbox hints from the class' contained window.
 * This is used while initializing the window to its first state, and not
 * thereafter.
 * Returns: true if the hints are successfully retreived and applied; false if
 * they are not.
 */
bool BlackboxWindow::getBlackboxHints(void) {
  int format;
  Atom atom_return;
  unsigned long num, len;
  BlackboxHints *blackbox_hint = 0;

  int ret = XGetWindowProperty(blackbox->getXDisplay(), client.window,
                               blackbox->getBlackboxHintsAtom(), 0,
                               PropBlackboxHintsElements, False,
                               blackbox->getBlackboxHintsAtom(), &atom_return,
                               &format, &num, &len,
                               (unsigned char **) &blackbox_hint);
  if (ret != Success || ! blackbox_hint || num != PropBlackboxHintsElements)
    return False;

  //  if (blackbox_hint->flags & AttribShaded)
  //    flags.shaded = (blackbox_hint->attrib & AttribShaded);

  if ((blackbox_hint->flags & AttribMaxHoriz) &&
      (blackbox_hint->flags & AttribMaxVert))
    flags.maximized = (blackbox_hint->attrib &
                       (AttribMaxHoriz | AttribMaxVert)) ? 1 : 0;
  else if (blackbox_hint->flags & AttribMaxVert)
    flags.maximized = (blackbox_hint->attrib & AttribMaxVert) ? 2 : 0;
  else if (blackbox_hint->flags & AttribMaxHoriz)
    flags.maximized = (blackbox_hint->attrib & AttribMaxHoriz) ? 3 : 0;

  if (blackbox_hint->flags & AttribWorkspace)
    blackbox_attrib.workspace = blackbox_hint->workspace;

  // if (blackbox_hint->flags & AttribStack)
  //   don't yet have always on top/bottom for blackbox yet... working
  //   on that

  if (blackbox_hint->flags & AttribDecoration) {
    switch (blackbox_hint->decoration) {
    case DecorNone:
      decorations = 0;

      break;

    case DecorTiny:
      decorations |= Decor_Titlebar | Decor_Iconify;
      decorations &= ~(Decor_Border | Decor_Handle | Decor_Maximize);
      functions &= ~(Func_Resize | Func_Maximize);

      break;

    case DecorTool:
      decorations |= Decor_Titlebar;
      decorations &= ~(Decor_Iconify | Decor_Border | Decor_Handle);
      functions &= ~(Func_Resize | Func_Maximize | Func_Iconify);

      break;

    case DecorNormal:
    default:
      decorations |= Decor_Titlebar | Decor_Border | Decor_Handle |
                     Decor_Iconify | Decor_Maximize;
      break;
    }

    reconfigure();
  }
  XFree(blackbox_hint);
  return True;
}


void BlackboxWindow::getTransientInfo(void) {
  if (client.transient_for &&
      client.transient_for != (BlackboxWindow *) ~0ul) {
    // reset transient_for in preparation of looking for a new owner
    client.transient_for->client.transientList.remove(this);
  }

  // we have no transient_for until we find a new one
  client.transient_for = (BlackboxWindow *) 0;

  Window trans_for;
  if (!XGetTransientForHint(blackbox->getXDisplay(), client.window,
                            &trans_for)) {
    // transient_for hint not set
    return;
  }

  if (trans_for == client.window) {
    // wierd client... treat this window as a normal window
    return;
  }

  if (trans_for == None || trans_for == screen->getRootWindow()) {
    // this is an undocumented interpretation of the ICCCM. a transient
    // associated with None/Root/itself is assumed to be a modal root
    // transient.  we don't support the concept of a global transient,
    // so we just associate this transient with nothing, and perhaps
    // we will add support later for global modality.
    client.transient_for = (BlackboxWindow *) ~0ul;
    flags.modal = True;
    return;
  }

  client.transient_for = blackbox->searchWindow(trans_for);
  if (! client.transient_for &&
      client.window_group && trans_for == client.window_group) {
    // no direct transient_for, perhaps this is a group transient?
    BWindowGroup *group = blackbox->searchGroup(client.window_group);
    if (group) client.transient_for = group->find(screen);
  }

  if (! client.transient_for || client.transient_for == this) {
    // no transient_for found, or we have a wierd client that wants to be
    // a transient for itself, so we treat this window as a normal window
    client.transient_for = (BlackboxWindow*) 0;
    return;
  }

  // Check for a circular transient state: this can lock up Blackbox
  // when it tries to find the non-transient window for a transient.
  BlackboxWindow *w = this;
  while(w->client.transient_for &&
        w->client.transient_for != (BlackboxWindow *) ~0ul) {
    if(w->client.transient_for == this) {
      client.transient_for = (BlackboxWindow*) 0;
      break;
    }
    w = w->client.transient_for;
  }

  if (client.transient_for) {
    // register ourselves with our new transient_for
    client.transient_for->client.transientList.push_back(this);
  }
}


BlackboxWindow *BlackboxWindow::getTransientFor(void) const {
  if (client.transient_for &&
      client.transient_for != (BlackboxWindow*) ~0ul)
    return client.transient_for;
  return 0;
}


/*
 * This function is responsible for updating both the client and the frame
 * rectangles.
 * According to the ICCCM a client message is not sent for a resize, only a
 * move.
 */
void BlackboxWindow::configure(int dx, int dy,
                               unsigned int dw, unsigned int dh) {
#if defined(DEBUG)
  fprintf(stderr, "configure req:%d %d %d %d - frame:%d %d %d %d\n",
         dx, dy, dw, dh,
         frame.rectFrame.left(),
         frame.rectFrame.top(),
         frame.rectFrame.width(),
         frame.rectFrame.height());
#endif

  bool send_event = ((frame.rectFrame.x() != dx || frame.rectFrame.y() != dy) &&
                     ! flags.moving);

  if (dw != frame.rectFrame.width() || dh != frame.rectFrame.height()) {
    frame.rectFrame.setRect(dx, dy, dw, dh);

    //if (fx < 0 || fy < 0)
    //frame.rect.setPos(frame.rect.x()*2 - fx, frame.rect.y()*2 - fy);

    client.rect.setRect(frame.rectFrame.left() + frame.margin.left,
                        frame.rectFrame.top() + frame.margin.top,
                        frame.rectFrame.width()
                        - (frame.margin.left + frame.margin.right),
                        frame.rectFrame.height()
                        - (frame.margin.top + frame.margin.bottom));

#ifdef    SHAPE
    if (blackbox->hasShapeExtensions() && flags.shaped) {
      configureShape();
    }
#endif // SHAPE

    positionWindows();
    decorate();
    redrawWindowFrame();
  } else {
    frame.rectFrame.setPos(dx, dy);

    XMoveWindow(blackbox->getXDisplay(), client.window,
                frame.rectFrame.x() + frame.margin.left,
                frame.rectFrame.y() + frame.margin.top);
    /*
      we may have been called just after an opaque window move, so even though
      the old coords match the new ones no ConfigureNotify has been sent yet.
      There are likely other times when this will be relevant as well.
    */
    if (! flags.moving) send_event = True;
  }
#if 0
  if (send_event) {
    // if moving, the update and event will occur when the move finishes
    client.rect.setPos(frame.rectFrame.left() + frame.margin.left,
                       frame.rectFrame.top() + frame.margin.top);

    XEvent event;
    event.type = ConfigureNotify;

    event.xconfigure.display = blackbox->getXDisplay();
    event.xconfigure.event = client.window;
    event.xconfigure.window = client.window;
    event.xconfigure.x = client.rect.x();
    event.xconfigure.y = client.rect.y();
    event.xconfigure.width = client.rect.width();
    event.xconfigure.height = client.rect.height();
    event.xconfigure.border_width = client.old_bw;
    event.xconfigure.above = frame.window;
    event.xconfigure.override_redirect = False;

    /*XSendEvent(blackbox->getXDisplay(), client.window, False,
      StructureNotifyMask, &event);*/
    screen->updateNetizenConfigNotify(&event);
    XFlush(blackbox->getXDisplay());
  }
#endif
#if defined(DEBUG)
  fprintf(stderr, "configure - done\n");
#endif
}


#ifdef SHAPE
void BlackboxWindow::configureShape(void) {
  //  XShapeCombineShape(blackbox->getXDisplay(), frame.window, ShapeBounding,
  //                     0, 0, client.window, ShapeBounding, ShapeSet);
}
#endif // SHAPE


bool BlackboxWindow::setInputFocus(void) {
  if (flags.focused) return True;

  // do not give focus to a window that is about to close
  if (! validateClient()) return False;

  assert(! flags.iconic &&
         (blackbox_attrib.workspace == screen->getCurrentWorkspaceID()));

  if (! frame.rectFrame.intersects(screen->getRect())) {
    // client is outside the screen, move it to the center
#if defined(DEBUG)
    fprintf(stderr, "frame.rectFrame.intersects(screen->getRect())\n"
            "\t%d %d\n\t %d %d %d %d\n",
            screen->getWidth(), screen->getHeight(),
            frame.rectFrame.x(), frame.rectFrame.y(),
            frame.rectFrame.width(), frame.rectFrame.height());
#endif
    /*configure((screen->getWidth() - frame.rectFrame.width()) / 2,
              (screen->getHeight() - frame.rectFrame.height()) / 2,
              frame.rectFrame.width(), frame.rectFrame.height());*/
  }

  if (client.transientList.size() > 0) {
    // transfer focus to any modal transients
    BlackboxWindowList::iterator it, end = client.transientList.end();
    for (it = client.transientList.begin(); it != end; ++it)
      if ((*it)->flags.modal) return (*it)->setInputFocus();
  }

  bool ret = True;
  switch (focus_mode) {
  case F_Passive:
  case F_LocallyActive:
    XSetInputFocus(blackbox->getXDisplay(), client.window,
                   RevertToPointerRoot, CurrentTime);
    blackbox->setFocusedWindow(this);
    break;

  case F_GloballyActive:
  case F_NoInput:
    /*
     * we could set the focus to none, since the window doesn't accept focus,
     * but we shouldn't set focus to nothing since this would surely make
     * someone angry
     */
    ret = False;
    break;
  }

  if (flags.send_focus_message) {
    XEvent ce;
    ce.xclient.type = ClientMessage;
    ce.xclient.message_type = blackbox->getWMProtocolsAtom();
    ce.xclient.display = blackbox->getXDisplay();
    ce.xclient.window = client.window;
    ce.xclient.format = 32;
    ce.xclient.data.l[0] = blackbox->getWMTakeFocusAtom();
    ce.xclient.data.l[1] = blackbox->getLastTime();
    ce.xclient.data.l[2] = 0l;
    ce.xclient.data.l[3] = 0l;
    ce.xclient.data.l[4] = 0l;
    XSendEvent(blackbox->getXDisplay(), client.window, False,
               NoEventMask, &ce);
    XFlush(blackbox->getXDisplay());
  }
  SetForegroundWindow((HWND)getHWnd(client.window));

  return ret;
}

//FIXME
void BlackboxWindow::iconify(void) {
  // walk up to the topmost transient_for that is not iconified
  if (isTransient() &&
      client.transient_for != (BlackboxWindow *) ~0ul &&
      ! client.transient_for->isIconic()) {

    client.transient_for->iconify();
    return;
  }

  if (flags.iconic) return;

  /*
   * unmap the frame window first, so when all the transients are
   * unmapped, we don't get an enter event in sloppy focus mode
   */
  //XWindowsWMFrameShowWindow (blackbox->getXDisplay(), 0, frame.window,
  //WindowsWMFrameSWMinimize);
  //XUnmapWindow(blackbox->getXDisplay(), frame.window);

  flags.visible = False;
  flags.iconic = True;

  setState(IconicState);

  // iconify all transients first
  if (client.transientList.size() > 0) {
    std::for_each(client.transientList.begin(), client.transientList.end(),
                  std::mem_fun(&BlackboxWindow::iconify));
  }

  /*
   * remove the window from the workspace and add it to the screen's
   * icons *AFTER* we have process all transients.  since we always
   * iconify transients, it's pointless to have focus reverted to one
   * of them (since they are above their transient_for) for a split
   * second
   */
  //screen->getWorkspace(blackbox_attrib.workspace)->removeWindow(this);
  //screen->addIcon(this);

  /*
   * we don't want this XUnmapWindow call to generate an UnmapNotify event, so
   * we need to clear the event mask on client.window for a split second.
   * HOWEVER, since X11 is asynchronous, the window could be destroyed in that
   * split second, leaving us with a ghost window... so, we need to do this
   * while the X server is grabbed
   */
  unsigned long event_mask = PropertyChangeMask | FocusChangeMask |
                             StructureNotifyMask;
  XGrabServer(blackbox->getXDisplay());
  XSelectInput(blackbox->getXDisplay(), client.window,
               event_mask & ~StructureNotifyMask);
  XUnmapWindow(blackbox->getXDisplay(), client.window);
  XSelectInput(blackbox->getXDisplay(), client.window, event_mask);
  XUngrabServer(blackbox->getXDisplay());

  /* Create taskbar icon
   * FIXME: Shold XWinWM directly create Win32 window instead of X window?
   */
  XGrabServer(blackbox->getXDisplay());
  XWindowsWMSelectInput(blackbox->getXDisplay(), 0);
  XMapWindow(blackbox->getXDisplay(), window_in_taskbar);
  std::string taskbar_title = "[*]" + client.icon_title;
  XWindowsWMFrameSetTitle(blackbox->getXDisplay(), 0, window_in_taskbar,
                          taskbar_title.length(), taskbar_title.c_str());

  XWindowsWMFrameDraw(blackbox->getXDisplay(), 0, window_in_taskbar,
                      WindowsWMFrameStylePopup,
                      WindowsWMFrameStyleExAppWindow,
                      client.rect.x(), client.rect.y(),
                      client.rect.width(), client.rect.height());

  HWND hWnd = (HWND)getHWnd(window_in_taskbar);
  if (hWnd) {
    ShowWindow (hWnd, SW_MINIMIZE);
  }

  XWindowsWMSelectInput(blackbox->getXDisplay(),
                        WindowsWMControllerNotifyMask
                        | WindowsWMActivationNotifyMask);
  XUngrabServer(blackbox->getXDisplay());
}


void BlackboxWindow::show(void) {
  current_state = NormalState;
  setState(current_state);

  XUnmapWindow(blackbox->getXDisplay(), window_in_taskbar);
  XMapWindow(blackbox->getXDisplay(), client.window);
  //XMapSubwindows(blackbox->getXDisplay(), frame.window);

#if defined(DEBUG)
  int real_x, real_y;
  Window child;
  XTranslateCoordinates(blackbox->getXDisplay(), client.window,
                        screen->getRootWindow(),
                        0, 0, &real_x, &real_y, &child);
  fprintf(stderr, "%s -- assumed: (%d, %d), real: (%d, %d)\n", getTitle(),
          client.rect.left(), client.rect.top(), real_x, real_y);
  //assert(client.rect.left() == real_x && client.rect.top() == real_y);
#endif

  flags.visible = True;
  flags.iconic = False;
#if 0
  Atom atom_return;
  int format;
  unsigned long ulfoo, nitems;
  HWND *phWnd;

  if ((XGetWindowProperty (blackbox->getXDisplay(), client.window,
                           blackbox->getWindowsWMNativeHWnd(),
                           0l, 1l, False, XA_INTEGER,
                           &atom_return, &format, &nitems, &ulfoo,
                           (unsigned char **) &phWnd) == Success)
      && phWnd)
    {
      //if (*phWnd) ShowWindow (*phWnd, SW_SHOWNORMAL);
    }
  else
    {
      fprintf(stderr, "XGetWindowProperty failed. %d %d %d %d %d\n",
              atom_return, format, nitems, ulfoo, phWnd);
    }
  XFree (phWnd);
#endif
}


void BlackboxWindow::deiconify(bool reassoc, bool raise) {
  if (flags.iconic || reassoc)
    screen->reassociateWindow(this, BSENTINEL, False);
  else if (blackbox_attrib.workspace != screen->getCurrentWorkspaceID())
    return;

  show();

  // reassociate and deiconify all transients
  if (reassoc && client.transientList.size() > 0) {
    BlackboxWindowList::iterator it, end = client.transientList.end();
    for (it = client.transientList.begin(); it != end; ++it)
      (*it)->deiconify(True, False);
  }

  if (raise)
    screen->getWorkspace(blackbox_attrib.workspace)->raiseWindow(this);
}


void BlackboxWindow::close(void) {
  XEvent ce;
  ce.xclient.type = ClientMessage;
  ce.xclient.message_type = blackbox->getWMProtocolsAtom();
  ce.xclient.display = blackbox->getXDisplay();
  ce.xclient.window = client.window;
  ce.xclient.format = 32;
  ce.xclient.data.l[0] = blackbox->getWMDeleteAtom();
  ce.xclient.data.l[1] = CurrentTime;
  ce.xclient.data.l[2] = 0l;
  ce.xclient.data.l[3] = 0l;
  ce.xclient.data.l[4] = 0l;
  XSendEvent(blackbox->getXDisplay(), client.window, False, NoEventMask, &ce);
  XFlush(blackbox->getXDisplay());
}


void BlackboxWindow::withdraw(void) {
  setState(current_state);

  flags.visible = False;
  flags.iconic = False;

  //XUnmapWindow(blackbox->getXDisplay(), client.window);
  XUnmapWindow(blackbox->getXDisplay(), window_in_taskbar);

  XGrabServer(blackbox->getXDisplay());

  unsigned long event_mask = PropertyChangeMask | FocusChangeMask |
                             StructureNotifyMask;
  XSelectInput(blackbox->getXDisplay(), client.window,
               event_mask & ~StructureNotifyMask);
  XUnmapWindow(blackbox->getXDisplay(), client.window);
  XSelectInput(blackbox->getXDisplay(), client.window, event_mask);

  XUngrabServer(blackbox->getXDisplay());
}


void BlackboxWindow::maximize(unsigned int button) {
  if (flags.maximized) {
    flags.maximized = 0;

    blackbox_attrib.flags &= ! (AttribMaxHoriz | AttribMaxVert);
    blackbox_attrib.attrib &= ! (AttribMaxHoriz | AttribMaxVert);

    fprintf(stderr, "BlackboxWindow::maximize - restore\n"
            "\t%d %d %d %d\n",
            blackbox_attrib.premax_x, blackbox_attrib.premax_y,
            blackbox_attrib.premax_w, blackbox_attrib.premax_h);

    //configure(blackbox_attrib.premax_x, blackbox_attrib.premax_y,
    //blackbox_attrib.premax_w, blackbox_attrib.premax_h);

    blackbox_attrib.premax_x = blackbox_attrib.premax_y = 0;
    blackbox_attrib.premax_w = blackbox_attrib.premax_h = 0;

    setState(current_state);
    return;
  }

  blackbox_attrib.premax_x = frame.rectFrame.x();
  blackbox_attrib.premax_y = frame.rectFrame.y();
  blackbox_attrib.premax_w = frame.rectFrame.width();
  blackbox_attrib.premax_h = frame.rectFrame.height();

  fprintf(stderr, "BlackboxWindow::maximize - \n"
          "\t%d %d %d %d\n",
          blackbox_attrib.premax_x, blackbox_attrib.premax_y,
          blackbox_attrib.premax_w, blackbox_attrib.premax_h);

  const Rect &screen_area = screen->availableArea();

  switch(button) {
  case 1:
    blackbox_attrib.flags |= AttribMaxHoriz | AttribMaxVert;
    blackbox_attrib.attrib |= AttribMaxHoriz | AttribMaxVert;
    break;

  case 2:
    assert(0);
    break;

  case 3:
    assert(0);
    break;
  }

  //constrain(TopLeft);

  flags.maximized = button;

  //configure(frame.changing.x(), frame.changing.y(),
  //frame.changing.width(), frame.changing.height());
  //redrawAllButtons(); // in case it is not called in configure()
  setState(current_state);
}


// re-maximizes the window to take into account availableArea changes
void BlackboxWindow::remaximize(void) {
}


void BlackboxWindow::setWorkspace(unsigned int n) {
  blackbox_attrib.flags |= AttribWorkspace;
  blackbox_attrib.workspace = n;
}


void BlackboxWindow::redrawWindowFrame(void) const {
  if (decorations & Decor_Titlebar) {
  }

  if (decorations & Decor_Handle) {
  }

  if (decorations & Decor_Border) {
  }
}


void BlackboxWindow::setFocusFlag(bool focus) {
  // only focus a window if it is visible
  if (focus && ! flags.visible)
    return;

  flags.focused = focus;

  redrawWindowFrame();

  if (flags.focused)
    blackbox->setFocusedWindow(this);
}


void BlackboxWindow::installColormap(bool install) {
  int i = 0, ncmap = 0;
  Colormap *cmaps = XListInstalledColormaps(blackbox->getXDisplay(),
                                            client.window, &ncmap);
  if (cmaps) {
    XWindowAttributes wattrib;
    if (XGetWindowAttributes(blackbox->getXDisplay(),
                             client.window, &wattrib)) {
      if (install) {
        // install the window's colormap
        for (i = 0; i < ncmap; i++) {
          if (*(cmaps + i) == wattrib.colormap)
            // this window is using an installed color map... do not install
            install = False;
        }
        // otherwise, install the window's colormap
        if (install)
          XInstallColormap(blackbox->getXDisplay(), wattrib.colormap);
      } else {
        // uninstall the window's colormap
        for (i = 0; i < ncmap; i++) {
          if (*(cmaps + i) == wattrib.colormap)
            // we found the colormap to uninstall
            XUninstallColormap(blackbox->getXDisplay(), wattrib.colormap);
        }
      }
    }

    XFree(cmaps);
  }
}


void BlackboxWindow::setState(unsigned long new_state) {
  current_state = new_state;

  unsigned long state[2];
  state[0] = current_state;
  state[1] = None;
  XChangeProperty(blackbox->getXDisplay(), client.window,
                  blackbox->getWMStateAtom(), blackbox->getWMStateAtom(), 32,
                  PropModeReplace, (unsigned char *) state, 2);

  XChangeProperty(blackbox->getXDisplay(), client.window,
                  blackbox->getBlackboxAttributesAtom(),
                  blackbox->getBlackboxAttributesAtom(), 32, PropModeReplace,
                  (unsigned char *) &blackbox_attrib,
                  PropBlackboxAttributesElements);
}


bool BlackboxWindow::getState(void) {
  current_state = 0;

  Atom atom_return;
  bool ret = False;
  int foo;
  unsigned long *state, ulfoo, nitems;

  if ((XGetWindowProperty(blackbox->getXDisplay(), client.window,
                          blackbox->getWMStateAtom(),
                          0l, 2l, False, blackbox->getWMStateAtom(),
                          &atom_return, &foo, &nitems, &ulfoo,
                          (unsigned char **) &state) != Success) ||
      (! state)) {
    return False;
  }

  if (nitems >= 1) {
    current_state = static_cast<unsigned long>(state[0]);

    ret = True;
  }

  XFree((void *) state);

  return ret;
}


void BlackboxWindow::restoreAttributes(void) {
  Atom atom_return;
  int foo;
  unsigned long ulfoo, nitems;

  BlackboxAttributes *net;
  int ret = XGetWindowProperty(blackbox->getXDisplay(), client.window,
                               blackbox->getBlackboxAttributesAtom(), 0l,
                               PropBlackboxAttributesElements, False,
                               blackbox->getBlackboxAttributesAtom(),
                               &atom_return, &foo, &nitems, &ulfoo,
                               (unsigned char **) &net);
  if (ret != Success || !net || nitems != PropBlackboxAttributesElements)
    return;

  if (net->workspace != screen->getCurrentWorkspaceID() &&
      net->workspace < screen->getWorkspaceCount()) {
    screen->reassociateWindow(this, net->workspace, True);

    // set to WithdrawnState so it will be mapped on the new workspace
    if (current_state == NormalState) current_state = WithdrawnState;
  } else if (current_state == WithdrawnState) {
    // the window is on this workspace and is Withdrawn, so it is waiting to
    // be mapped
    current_state = NormalState;
  }

  if (net->flags & AttribMaxHoriz || net->flags & AttribMaxVert) {
    blackbox_attrib.premax_x = net->premax_x;
    blackbox_attrib.premax_y = net->premax_y;
    blackbox_attrib.premax_w = net->premax_w;
    blackbox_attrib.premax_h = net->premax_h;

    flags.maximized = 0;

    if (net->flags & AttribMaxHoriz && net->flags & AttribMaxVert &&
        net->attrib & (AttribMaxHoriz | AttribMaxVert))
      flags.maximized = 1;
    else if (net->flags & AttribMaxVert && net->attrib & AttribMaxVert)
        flags.maximized = 2;
    else if (net->flags & AttribMaxHoriz && net->attrib & AttribMaxHoriz)
        flags.maximized = 3;

    if (flags.maximized) remaximize();
  }

  if (net->flags & AttribDecoration) {
    switch (net->decoration) {
    case DecorNone:
      decorations = 0;

      break;

    default:
    case DecorNormal:
      decorations |= Decor_Titlebar | Decor_Handle | Decor_Border |
        Decor_Iconify | Decor_Maximize;

      break;

    case DecorTiny:
      decorations |= Decor_Titlebar | Decor_Iconify;
      decorations &= ~(Decor_Border | Decor_Handle | Decor_Maximize);

      break;

    case DecorTool:
      decorations |= Decor_Titlebar;
      decorations &= ~(Decor_Iconify | Decor_Border | Decor_Handle);

      break;
    }

    // sanity check the new decor
    if (! (functions & Func_Resize) || isTransient())
      decorations &= ~(Decor_Maximize | Decor_Handle);
    if (! (functions & Func_Maximize))
      decorations &= ~Decor_Maximize;

    if (decorations & Decor_Titlebar) {
      if (functions & Func_Close)   // close button is controlled by function
        decorations |= Decor_Close; // not decor type
    }

    if (flags.visible && client.window) {
      XMapWindow(blackbox->getXDisplay(), client.window);
    }

    reconfigure();
    setState(current_state);
  }

  // with the state set it will then be the map event's job to read the
  // window's state and behave accordingly

  XFree((void *) net);
}


/*
 * Positions the Rect r according the the client window position and
 * window gravity.
 */
void BlackboxWindow::applyGravity(Rect &r) {
#if defined(DEBUG)
  fprintf(stderr, "applyGravity %d\n"
          "\t%d %d %d %d\n\t %d %d %d %d\n",
          client.win_gravity,
          client.rect.x(), client.rect.y(),
          client.rect.width(), client.rect.height(),
          frame.margin.left, frame.margin.top,
          frame.margin.right, frame.margin.bottom);
#endif
  // apply horizontal window gravity
  switch (client.win_gravity) {
  default:
  case NorthWestGravity:
  case SouthWestGravity:
  case WestGravity:
    r.setX(client.rect.x());
    break;

  case NorthGravity:
  case SouthGravity:
  case CenterGravity:
    r.setX(client.rect.x() - (frame.margin.left + frame.margin.right) / 2);
    break;

  case NorthEastGravity:
  case SouthEastGravity:
  case EastGravity:
    r.setX(client.rect.x() - (frame.margin.left + frame.margin.right) + 2);
    break;

  case ForgetGravity:
  case StaticGravity:
    r.setX(client.rect.x() - frame.margin.left);
    break;
  }

  // apply vertical window gravity
  switch (client.win_gravity) {
  default:
  case NorthWestGravity:
  case NorthEastGravity:
  case NorthGravity:
    r.setY(client.rect.y());
    break;

  case CenterGravity:
  case EastGravity:
  case WestGravity:
    r.setY(client.rect.y() - (frame.margin.top + frame.margin.bottom) / 2);
    break;

  case SouthWestGravity:
  case SouthEastGravity:
  case SouthGravity:
    r.setY(client.rect.y() - (frame.margin.bottom + frame.margin.top) + 2);
    break;

  case ForgetGravity:
  case StaticGravity:
    r.setY(client.rect.y() - frame.margin.top);
    break;
  }
#if defined(DEBUG)
  fprintf(stderr, "applyGravity - done %d %d %d %d\n",
          r.x(), r.y(), r.width(), r.height());
#endif
}


/*
 * The reverse of the applyGravity function.
 *
 * Positions the Rect r according to the frame window position and
 * window gravity.
 */
void BlackboxWindow::restoreGravity(Rect &r) {
  // restore horizontal window gravity
  switch (client.win_gravity) {
  default:
  case NorthWestGravity:
  case SouthWestGravity:
  case WestGravity:
    r.setX(frame.rectFrame.x());
    break;

  case NorthGravity:
  case SouthGravity:
  case CenterGravity:
    r.setX(frame.rectFrame.x() + (frame.margin.left + frame.margin.right) / 2);
    break;

  case NorthEastGravity:
  case SouthEastGravity:
  case EastGravity:
    r.setX(frame.rectFrame.x() + (frame.margin.right + frame.margin.left) - 2);
    break;

  case ForgetGravity:
  case StaticGravity:
    r.setX(frame.rectFrame.x() + frame.margin.left);
    break;
  }

  // restore vertical window gravity
  switch (client.win_gravity) {
  default:
  case NorthWestGravity:
  case NorthEastGravity:
  case NorthGravity:
    r.setY(frame.rectFrame.y());
    break;

  case CenterGravity:
  case EastGravity:
  case WestGravity:
    r.setY(frame.rectFrame.y() + (frame.margin.top + frame.margin.bottom) / 2);
    break;

  case SouthWestGravity:
  case SouthEastGravity:
  case SouthGravity:
    r.setY(frame.rectFrame.y() + (frame.margin.top + frame.margin.bottom) - 2);
    break;

  case ForgetGravity:
  case StaticGravity:
    r.setY(frame.rectFrame.y() + frame.margin.top);
    break;
  }
}


void BlackboxWindow::redrawLabel(void) const {
  //WindowsWM ext
#if defined(DEBUG)
  fprintf(stderr, "XWindowsWMFrameSetTitle %s\n", client.title.c_str());
#endif
  XWindowsWMFrameSetTitle(blackbox->getXDisplay(), 0, client.window,
                          client.title.length(), client.title.c_str());
}


void BlackboxWindow::redrawAllButtons(void) const {
}


void BlackboxWindow::redrawIconifyButton(bool /*pressed*/) const {
}


void BlackboxWindow::redrawMaximizeButton(bool /*pressed*/) const {
}


void BlackboxWindow::redrawCloseButton(bool /*pressed*/) const {
}

//FIXME:
void BlackboxWindow::mapRequestEvent(const XMapRequestEvent *re) {
  if (re->window != client.window)
    return;

#if defined(DEBUG)
  fprintf(stderr, "BlackboxWindow::mapRequestEvent() for 0x%lx\n",
          client.window);
#endif // DEBUG

  switch (current_state) {
  case IconicState:
    iconify();
    break;

  case WithdrawnState:
    withdraw();
    break;

  case NormalState:
  case InactiveState:
  case ZoomState:
  default:
    show();
    screen->getWorkspace(blackbox_attrib.workspace)->raiseWindow(this);
    if (! blackbox->isStartup() && (isTransient() || screen->doFocusNew())) {
      XSync(blackbox->getXDisplay(), False); // make sure the frame is mapped..
      setInputFocus();
    }
    break;
  }
}


void BlackboxWindow::unmapNotifyEvent(const XUnmapEvent *ue) {
  if (ue->window != client.window)
    return;

#if defined(DEBUG)
  fprintf(stderr, "BlackboxWindow::unmapNotifyEvent() for 0x%lx\n",
          client.window);
#endif // DEBUG

  screen->unmanageWindow(this, False);
}


void BlackboxWindow::destroyNotifyEvent(const XDestroyWindowEvent *de) {
  if (de->window != client.window)
    return;

#if defined(DEBUG)
  fprintf(stderr, "BlackboxWindow::destroyNotifyEvent() for 0x%lx\n",
          client.window);
#endif // DEBUG

  screen->unmanageWindow(this, False);
}


void BlackboxWindow::reparentNotifyEvent(const XReparentEvent *re) {
  if (re->window != client.window)
    return;

#if defined(DEBUG)
  fprintf(stderr, "BlackboxWindow::reparentNotifyEvent(): reparent 0x%lx to "
          "0x%lx.\n", client.window, re->parent);
#endif // DEBUG

  XEvent ev;
  ev.xreparent = *re;
  XPutBackEvent(blackbox->getXDisplay(), &ev);
  screen->unmanageWindow(this, True);
}


void BlackboxWindow::propertyNotifyEvent(const XPropertyEvent *pe) {
  if (pe->state == PropertyDelete || ! validateClient())
    return;

#if defined(DEBUG)
  fprintf(stderr, "BlackboxWindow::propertyNotifyEvent(): for 0x%lx\n",
          client.window);
#endif

  switch(pe->atom) {
  case XA_WM_CLASS:
    getWMClass();
    break;

  case XA_WM_CLIENT_MACHINE:
  case XA_WM_COMMAND:
    break;

  case XA_WM_TRANSIENT_FOR: {
    // determine if this is a transient window
    getTransientInfo();

    // adjust the window decorations based on transience
    if (isTransient()) {
      decorations &= ~(Decor_Maximize | Decor_Handle);
      functions &= ~Func_Maximize;
    }

    reconfigure();
  }
    break;

  case XA_WM_HINTS:
    getWMHints();
    break;

  case XA_WM_ICON_NAME:
    getWMIconName();
    if (flags.iconic) screen->propagateWindowName(this);
    break;

  case XA_WM_NAME:
    getWMName();

    if (decorations & Decor_Titlebar)
      redrawLabel();

    screen->propagateWindowName(this);
    break;

  case XA_WM_NORMAL_HINTS: {
    getWMNormalHints();

    if ((client.normal_hint_flags & PMinSize) &&
        (client.normal_hint_flags & PMaxSize)) {
      // the window now can/can't resize itself, so the buttons need to be
      // regrabbed.
      ungrabButtons();
      if (client.max_width <= client.min_width &&
          client.max_height <= client.min_height) {
        decorations &= ~(Decor_Maximize | Decor_Handle);
        functions &= ~(Func_Resize | Func_Maximize);
      } else {
        if (! isTransient()) {
          decorations |= Decor_Maximize | Decor_Handle;
          functions |= Func_Maximize;
        }
        functions |= Func_Resize;
      }
      grabButtons();
    }

    Rect old_rect = frame.rectFrame;

    upsize();

    if (old_rect != frame.rectFrame)
      reconfigure();

    break;
  }

  default:
    if (pe->atom == blackbox->getWMProtocolsAtom()) {
      getWMProtocols();
/*
      if ((decorations & Decor_Close) && (! frame.close_button)) {
        createCloseButton();
        if (decorations & Decor_Titlebar) {
          positionButtons(True);
          XMapSubwindows(blackbox->getXDisplay(), frame.title);
        }
        if (windowmenu) windowmenu->reconfigure();
      }*/
    }

    break;
  }
}


void BlackboxWindow::exposeEvent(const XExposeEvent */*ee*/) {
#if defined(DEBUG)
  fprintf(stderr, "BlackboxWindow::exposeEvent() for 0x%lx\n", client.window);
#endif
}

//FIXME:
void BlackboxWindow::configureRequestEvent(const XConfigureRequestEvent *cr) {
  if (cr->window == client.window && !flags.iconic) {
#if defined(DEBUG)
    fprintf(stderr, "configureRequestEvent cr:%d %d %d %d vask:%08x frame now:%d %d %d %d\n",
            cr->x, cr->y, cr->width, cr->height,
            cr->value_mask,
            frame.rectFrame.left(),
            frame.rectFrame.top(),
            frame.rectFrame.right(),
            frame.rectFrame.bottom());
#endif

    if (cr->value_mask & CWBorderWidth)
      client.old_bw = cr->border_width;

    if (cr->value_mask & (CWX | CWY | CWWidth | CWHeight)) {

      //Don't touch window size and position, if window is maximized. It restores window.
      if (flags.maximized)
        {
#if defined(DEBUG)
          fprintf(stderr, "configureRequestEvent - maximized");
#endif
          short fx, fy, fw, fh;
          XWindowsWMFrameGetRect(blackbox->getXDisplay(),
                                 frame_style, frame_style_ex, 0,
                                 cr->x, cr->y, cr->width, cr->height,
                                 &fx, &fy, &fw, &fh);
          frame.rectFrame.setRect(fx, fy, fw, fh);

          XWindowChanges wc;
          wc.x = cr->x;
          wc.y = cr->y;
          wc.width = cr->width;
          wc.height = cr->height;

          XConfigureWindow (blackbox->getXDisplay(), client.window,
                            cr->value_mask & (CWX|CWY|CWWidth|CWHeight), &wc);
          return;
        }
      Rect req = frame.rectFrame;

      if (cr->value_mask & (CWX | CWY)) {
        if (cr->value_mask & CWX)
          client.rect.setX(cr->x);
        if (cr->value_mask & CWY)
          client.rect.setY(cr->y);
        req.setX(client.rect.x() - frame.margin.left);
        req.setY(client.rect.y() - frame.margin.top);
#if defined(DEBUG)
        fprintf(stderr, "configureRequestEvent CX/CY\n"
                "\tclient:%d %d %d %d\n"
                "\treq:%d %d %d %d\n",
                client.rect.left(),
                client.rect.top(),
                client.rect.right(),
                client.rect.bottom(),
                req.left(),
                req.top(),
                req.right(),
                req.bottom());
#endif
        //applyGravity(req);
      }

      if (cr->value_mask & CWWidth)
        req.setWidth(cr->width + frame.margin.left + frame.margin.right);

      if (cr->value_mask & CWHeight)
        req.setHeight(cr->height + frame.margin.top + frame.margin.bottom);

      //      if (!flags.maximized)
      configure(req.x(), req.y(), req.width(), req.height());
#if 0
      XWindowChanges wc;
      wc.x = cr->x;
      wc.y = cr->y;
      wc.width = cr->width;
      wc.height = cr->height;

      XConfigureWindow (blackbox->getXDisplay(), client.window,
                        cr->value_mask & (CWX|CWY|CWWidth|CWHeight), &wc);
#endif
    }
    if (cr->value_mask & CWStackMode) {
#if 0
      XWindowChanges wc;
      wc.sibling = cr->above;
      wc.stack_mode = cr->detail;
      
      XConfigureWindow (blackbox->getXDisplay(), client.window,
                        cr->value_mask & (CWSibling | CWStackMode), &wc);
#else
      switch (cr->detail) {
      case Below:
      case BottomIf:
#if defined(DEBUG)
        fprintf(stderr, "configureRequestEvent CWStackMode %d Below BottomIf\n",
                cr->detail);
#endif
        screen->getWorkspace(blackbox_attrib.workspace)->lowerWindow(this);
        break;
        
      case Above:
      case TopIf:
#if defined(DEBUG)
        fprintf(stderr, "configureRequestEvent CWStackMode %d Above TopIf\n",
                cr->detail);
#endif
        screen->getWorkspace(blackbox_attrib.workspace)->raiseWindow(this);
        break;
      default:
#if defined(DEBUG)
        fprintf(stderr, "configureRequestEvent CWStackMode %d default\n",
                cr->detail);
#endif
        screen->getWorkspace(blackbox_attrib.workspace)->raiseWindow(this);
        break;
      }
#endif
    }
  } else if (cr->window == window_in_taskbar) {
#if defined(DEBUG)
    fprintf(stderr, "configureRequestEvent taskbar cr:%d %d %d %d - frame now:%d %d %d %d\n",
            cr->x, cr->y, cr->width, cr->height,
            frame.rectFrame.left(),
            frame.rectFrame.top(),
            frame.rectFrame.right(),
            frame.rectFrame.bottom());
#endif
  }
}


void BlackboxWindow::configureNotifyEvent(const XConfigureEvent *ce) {
  if (ce->window != client.window)
    return;
#if 0
#if defined(DEBUG)
  fprintf(stderr, "BlackboxWindow::configureNotifyEvent\n");
#endif
  HWND hWnd = (HWND)getHWnd(client.window);
  if (hWnd && blackbox_attrib.premax_w != 0 && blackbox_attrib.premax_h != 0) {
#if defined(DEBUG)
    fprintf(stderr, "\t%d %d %d %d\n",
            blackbox_attrib.premax_x, blackbox_attrib.premax_y,
            blackbox_attrib.premax_w, blackbox_attrib.premax_h);
#endif
    WINDOWPLACEMENT wndpl = {sizeof(WINDOWPLACEMENT)};
    if (GetWindowPlacement(hWnd, &wndpl)) {
      SetRect (&wndpl.rcNormalPosition,
               blackbox_attrib.premax_x, blackbox_attrib.premax_y,
               blackbox_attrib.premax_x + blackbox_attrib.premax_w,
               blackbox_attrib.premax_y + blackbox_attrib.premax_h);

      if (!SetWindowPlacement(hWnd, &wndpl)) {
        fprintf(stderr, "SetWindowPlacement failed\n");
      }
    } else {
      fprintf(stderr, "GetWindowPlacement failed\n");
    }
    //ShowWindow (hWnd, SW_MINIMIZE);
  }
#endif
}


void BlackboxWindow::buttonPressEvent(const XButtonEvent */*be*/) {
#if defined(DEBUG)
  fprintf(stderr, "BlackboxWindow::buttonPressEvent() for 0x%lx\n",
          client.window);
#endif
#if 0
  if (frame.maximize_button == be->window) {
    redrawMaximizeButton(True);
  } else if (be->button == 1 || (be->button == 3 && be->state == Mod1Mask)) {
    if (! flags.focused)
      setInputFocus();

    if (frame.iconify_button == be->window) {
      redrawIconifyButton(True);
    } else if (frame.close_button == be->window) {
      redrawCloseButton(True);
    }
#if 0
    else if (frame.window == be->window) {
      screen->getWorkspace(blackbox_attrib.workspace)->raiseWindow(this);

      XAllowEvents(blackbox->getXDisplay(), ReplayPointer, be->time);
    }
#endif
    else {
      if (frame.title == be->window || frame.label == be->window) {
        if (((be->time - lastButtonPressTime) <=
             blackbox->getDoubleClickInterval()) ||
            (be->state == ControlMask)) {
          lastButtonPressTime = 0;
          shade();
        } else {
          lastButtonPressTime = be->time;
        }
      }

      frame.grab_x = be->x_root - frame.rect.x() - frame.border_w;
      frame.grab_y = be->y_root - frame.rect.y() - frame.border_w;

      screen->getWorkspace(blackbox_attrib.workspace)->raiseWindow(this);
    }
  } else if (be->button == 2 && (be->window != frame.iconify_button) &&
             (be->window != frame.close_button)) {
    screen->getWorkspace(blackbox_attrib.workspace)->lowerWindow(this);
  }
#endif
}


void BlackboxWindow::buttonReleaseEvent(const XButtonEvent */*re*/) {
#if defined(DEBUG)
  fprintf(stderr, "BlackboxWindow::buttonReleaseEvent() for 0x%lx\n",
          client.window);
#endif
#if 0
  if (re->window == frame.maximize_button) {
    if ((re->x >= 0 && re->x <= static_cast<signed>(frame.button_w)) &&
        (re->y >= 0 && re->y <= static_cast<signed>(frame.button_w))) {
      maximize(re->button);
      screen->getWorkspace(blackbox_attrib.workspace)->raiseWindow(this);
    } else {
      redrawMaximizeButton(flags.maximized);
    }
  } else if (re->window == frame.iconify_button) {
    if ((re->x >= 0 && re->x <= static_cast<signed>(frame.button_w)) &&
        (re->y >= 0 && re->y <= static_cast<signed>(frame.button_w))) {
      iconify();
    } else {
      redrawIconifyButton(False);
    }
  } else if (re->window == frame.close_button) {
    if ((re->x >= 0 && re->x <= static_cast<signed>(frame.button_w)) &&
        (re->y >= 0 && re->y <= static_cast<signed>(frame.button_w)))
      close();
    redrawCloseButton(False);
  } else if (flags.moving) {
    flags.moving = False;

    if (! screen->doOpaqueMove()) {
      /* when drawing the rubber band, we need to make sure we only draw inside
       * the frame... frame.changing_* contain the new coords for the window,
       * so we need to subtract 1 from changing_w/changing_h every where we
       * draw the rubber band (for both moving and resizing)
       */
      XDrawRectangle(blackbox->getXDisplay(), screen->getRootWindow(),
                     screen->getOpGC(), frame.changing.x(), frame.changing.y(),
                     frame.changing.width() - 1, frame.changing.height() - 1);
      XUngrabServer(blackbox->getXDisplay());

      configure(frame.changing.x(), frame.changing.y(),
                frame.changing.width(), frame.changing.height());
    } else {
      configure(frame.rect.x(), frame.rect.y(),
                frame.rect.width(), frame.rect.height());
    }
    XUngrabPointer(blackbox->getXDisplay(), CurrentTime);
  } else if (flags.resizing) {
    XDrawRectangle(blackbox->getXDisplay(), screen->getRootWindow(),
                   screen->getOpGC(), frame.changing.x(), frame.changing.y(),
                   frame.changing.width() - 1, frame.changing.height() - 1);
    XUngrabServer(blackbox->getXDisplay());

    constrain((re->window == frame.left_grip) ? TopRight : TopLeft);

    // unset maximized state when resized after fully maximized
    if (flags.maximized == 1)
      maximize(0);
    flags.resizing = False;
    configure(frame.changing.x(), frame.changing.y(),
              frame.changing.width(), frame.changing.height());

    XUngrabPointer(blackbox->getXDisplay(), CurrentTime);
  }
#if 0
  else if (re->window == frame.window) {
    if (re->button == 2 && re->state == Mod1Mask)
      XUngrabPointer(blackbox->getXDisplay(), CurrentTime);
  }
#endif
#endif
}


void BlackboxWindow::motionNotifyEvent(const XMotionEvent */*me*/) {
#if defined(DEBUG)
  fprintf(stderr, "BlackboxWindow::motionNotifyEvent() for 0x%lx\n",
          client.window);
#endif
}


void BlackboxWindow::enterNotifyEvent(const XCrossingEvent* ce) {
  if (! (screen->isSloppyFocus() && isVisible()))
    return;

  XEvent e;
  bool leave = False, inferior = False;

  while (XCheckTypedWindowEvent(blackbox->getXDisplay(), ce->window,
                                LeaveNotify, &e)) {
    if (e.type == LeaveNotify && e.xcrossing.mode == NotifyNormal) {
      leave = True;
      inferior = (e.xcrossing.detail == NotifyInferior);
    }
  }

  if ((! leave || inferior) && ! isFocused()) {
    bool success = setInputFocus();
    if (success)    // if focus succeeded install the colormap
      installColormap(True); // XXX: shouldnt we honour no install?
  }

  if (screen->doAutoRaise())
    timer->start();
}


void BlackboxWindow::leaveNotifyEvent(const XCrossingEvent*) {
  if (! (screen->isSloppyFocus() && screen->doAutoRaise()))
    return;

  installColormap(False);

  if (timer->isTiming())
    timer->stop();
}


#ifdef    SHAPE
void BlackboxWindow::shapeEvent(XShapeEvent *) {
  if (blackbox->hasShapeExtensions() && flags.shaped) {
    configureShape();
  }
}
#endif // SHAPE


void BlackboxWindow::windowsWMControllerEvent(XWindowsWMNotifyEvent *windows_wm_event) {
#if defined(DEBUG)
  fprintf (stderr, "\tBlackboxWindow::windowsWMControllerEvent %d %d %s\n",
           blackbox->hasWindowsWMExtensions(), windows_wm_event->arg,
           client.title.c_str());
#endif
  if (windows_wm_event->window == client.window)
    {
#if defined(DEBUG)
      fprintf (stderr, "\tclient window\n");
#endif
      if (blackbox->hasWindowsWMExtensions()) {
        switch (windows_wm_event->arg) {
        case WindowsWMMaximizeWindow:
          //printf ("maximize 0x%08x\n", (int)c);
          //raise_win(c);
          //show();
#if defined(DEBUG)
          fprintf (stderr, "\tWindowsWMMaximizeWindow\n");
#endif
          if (!flags.maximized) maximize(1);
          break;
        case WindowsWMMinimizeWindow:
          //printf ("minimize 0x%08x\n", (int)c);
          iconify();//FIXME: This window become hide. Configure this behavior by ext?
#if defined(DEBUG)
          fprintf (stderr,"\tWindowsWMMinimizeWindow\n");
#endif
          //FIXME:
          //screen->getWorkspace(blackbox_attrib.workspace)->lowerWindow(this);
          break;
        case WindowsWMRestoreWindow:
#if defined(DEBUG)
          fprintf (stderr, "\tWindowsWMRestoreWindow\n");
#endif
          //assert(0);
          //show();
          if (flags.maximized) maximize(1);
          break;
        case WindowsWMCloseWindow:
#if defined(DEBUG)
          fprintf (stderr, "\tWindowsWMCloseWindow\n");
#endif
          close();
          break;
        case WindowsWMActivateWindow:
#if defined(DEBUG)
          fprintf (stderr, "\tWindowsWMActivateWindow\n");
#endif
          //show();
          if (! flags.focused && !flags.iconic)
            setInputFocus();
          break;
        default:
          break;
        }
      }
    }

  if (windows_wm_event->window == window_in_taskbar)
    {
#if defined(DEBUG)
  fprintf (stderr, "\ttaskbar\n");
#endif
      if (blackbox->hasWindowsWMExtensions()) {
        switch (windows_wm_event->arg) {
        case WindowsWMMaximizeWindow:
          //show();
#if defined(DEBUG)
          fprintf (stderr, "\tWindowsWMMaximizeWindow\n");
#endif
          break;
        case WindowsWMMinimizeWindow:
          //iconify();
          assert(0);
#if defined(DEBUG)
          fprintf (stderr,"\tWindowsWMMinimizeWindow\n");
#endif
          break;
        case WindowsWMRestoreWindow:
#if defined(DEBUG)
          fprintf (stderr, "\tWindowsWMRestoreWindow %d\n", flags.iconic);
#endif
          show();
          break;
        case WindowsWMCloseWindow:
#if defined(DEBUG)
          fprintf (stderr, "\tWindowsWMCloseWindow\n");
#endif
          close();
          break;
        case WindowsWMActivateWindow:
#if defined(DEBUG)
          fprintf (stderr, "\tWindowsWMActivateWindow\n");
#endif
          //show();
          if (! flags.focused && !flags.iconic)
            setInputFocus();
          break;
        default:
          break;
        }
      }
    }
}


bool BlackboxWindow::validateClient(void) const {
  XSync(blackbox->getXDisplay(), False);

  XEvent e;
  if (XCheckTypedWindowEvent(blackbox->getXDisplay(), client.window,
                             DestroyNotify, &e) ||
      XCheckTypedWindowEvent(blackbox->getXDisplay(), client.window,
                             UnmapNotify, &e)) {
    XPutBackEvent(blackbox->getXDisplay(), &e);

    return False;
  }

  return True;
}


void BlackboxWindow::restore(bool remap) {
  XChangeSaveSet(blackbox->getXDisplay(), client.window, SetModeDelete);
  XSelectInput(blackbox->getXDisplay(), client.window, NoEventMask);

  setState(current_state);

  restoreGravity(client.rect);

  XUnmapWindow(blackbox->getXDisplay(), window_in_taskbar);
  XUnmapWindow(blackbox->getXDisplay(), client.window);

  XSetWindowBorderWidth(blackbox->getXDisplay(), client.window, client.old_bw);

  XEvent ev;
  if (XCheckTypedWindowEvent(blackbox->getXDisplay(), client.window,
                             ReparentNotify, &ev)) {
    remap = True;
  } else {
    // according to the ICCCM - if the client doesn't reparent to
    // root, then we have to do it for them
    XReparentWindow(blackbox->getXDisplay(), client.window,
                    screen->getRootWindow(),
                    client.rect.x(), client.rect.y());
  }
  XWindowsWMFrameDraw(blackbox->getXDisplay(), 0, client.window,
                      WindowsWMFrameStylePopup | WindowsWMFrameStyleClipChildren,
                      WindowsWMFrameStyleExToolWindow,
                      client.rect.x(), client.rect.y(),
                      client.rect.width(), client.rect.height());

  if (remap) XMapWindow(blackbox->getXDisplay(), client.window);
}


// timer for autoraise
void BlackboxWindow::timeout(void) {
  screen->getWorkspace(blackbox_attrib.workspace)->raiseWindow(this);
}


void BlackboxWindow::changeBlackboxHints(const BlackboxHints *net) {

  if (flags.visible && // watch out for requests when we can not be seen
      (net->flags & (AttribMaxVert | AttribMaxHoriz)) &&
      ((blackbox_attrib.attrib & (AttribMaxVert | AttribMaxHoriz)) !=
       (net->attrib & (AttribMaxVert | AttribMaxHoriz)))) {
    if (flags.maximized) {
      maximize(0);
    } else {
      int button = 0;

      if (net->flags & AttribMaxHoriz && net->flags & AttribMaxVert &&
          net->attrib & (AttribMaxHoriz | AttribMaxVert))
        button = 1;
      else if (net->flags & AttribMaxVert && net->attrib & AttribMaxVert)
        button = 2;
      else if (net->flags & AttribMaxHoriz && net->attrib & AttribMaxHoriz)
        button = 3;

      maximize(button);
    }
  }

  if ((net->flags & AttribWorkspace) &&
      (blackbox_attrib.workspace != net->workspace)) {
    screen->reassociateWindow(this, net->workspace, True);

    if (screen->getCurrentWorkspaceID() != net->workspace) {
      withdraw();
    } else {
      show();
      screen->getWorkspace(blackbox_attrib.workspace)->raiseWindow(this);
    }
  }

  if (net->flags & AttribDecoration) {
    switch (net->decoration) {
    case DecorNone:
      decorations = 0;

      break;

    default:
    case DecorNormal:
      decorations |= Decor_Titlebar | Decor_Handle | Decor_Border |
        Decor_Iconify | Decor_Maximize;

      break;

    case DecorTiny:
      decorations |= Decor_Titlebar | Decor_Iconify;
      decorations &= ~(Decor_Border | Decor_Handle | Decor_Maximize);

      break;

    case DecorTool:
      decorations |= Decor_Titlebar;
      decorations &= ~(Decor_Iconify | Decor_Border | Decor_Handle);

      break;
    }

    // sanity check the new decor
    if (! (functions & Func_Resize) || isTransient())
      decorations &= ~(Decor_Maximize | Decor_Handle);
    if (! (functions & Func_Maximize))
      decorations &= ~Decor_Maximize;
    if (! (functions & Func_Iconify))
      decorations &= ~Decor_Iconify;
    if (decorations & Decor_Titlebar) {
      if (functions & Func_Close)   // close button is controlled by function
        decorations |= Decor_Close; // not decor type
    }
#if 0
    if (flags.visible && frame.window) {
      XMapSubwindows(blackbox->getXDisplay(), frame.window);
      XMapWindow(blackbox->getXDisplay(), frame.window);
    }
#endif
    reconfigure();
    setState(current_state);
  }
}


/*
 * Set the sizes of all components of the window frame
 * (the window decorations).
 * These values are based upon the current style settings and the client
 * window's dimensions.
 */
void BlackboxWindow::upsize(void) {
#if 1
  if (decorations & Decor_Titlebar) {
    frame_style |= WindowsWMFrameStyleCaption | WindowsWMFrameStyleSysMenu;
  } else {
    frame_style &= ~(WindowsWMFrameStyleCaption | WindowsWMFrameStyleSysMenu);
  }

  if (decorations & Decor_Border) {
    frame_style |= WindowsWMFrameStyleBorder;
  } else {
    frame_style &= ~WindowsWMFrameStyleBorder;
  }

  if (decorations & Decor_Handle) {
    frame_style |= WindowsWMFrameStyleSizeBox;
  } else {
    frame_style &= ~WindowsWMFrameStyleSizeBox;
  }
#endif
  short fx, fy, fw, fh;
  XWindowsWMFrameGetRect(blackbox->getXDisplay(),
                         frame_style, frame_style_ex, 0,
                         client.rect.x(), client.rect.y(),
                         client.rect.width(), client.rect.height(),
                         &fx, &fy, &fw, &fh);
  frame.rectFrame.setRect(fx, fy, fw, fh);
#if defined(DEBUG)
  fprintf(stderr, "upsize\n"
          "\t%d %d %d %d\n"
          "\t%d %d %d %d\n",
          client.rect.x(), client.rect.y(),
          client.rect.width(), client.rect.height(),
          frame.rectFrame.x(), frame.rectFrame.y(),
          frame.rectFrame.width(), frame.rectFrame.height());
#endif
  frame.margin.left = client.rect.x() - fx;
  assert(frame.margin.left >= 0);

  frame.margin.right = fx + fw - (client.rect.x() + client.rect.width());
  assert(frame.margin.right >= 0);

  frame.margin.top = client.rect.y() - fy;
  assert(frame.margin.top >= 0);

  frame.margin.bottom = fy + fh - (client.rect.y() + client.rect.height());
  assert(frame.margin.bottom >= 0);
}


/*
 * Calculate the size of the client window and constrain it to the
 * size specified by the size hints of the client window.
 *
 * The logical width and height are placed into pw and ph, if they
 * are non-zero.  Logical size refers to the users perception of
 * the window size (for example an xterm resizes in cells, not in pixels).
 * pw and ph are then used to display the geometry during window moves, resize,
 * etc.
 *
 * The physical geometry is placed into frame.changing_{x,y,width,height}.
 * Physical geometry refers to the geometry of the window in pixels.
 */
void BlackboxWindow::constrain(Corner anchor,
                               unsigned int *pw, unsigned int *ph) {
#if 0
  // frame.changing represents the requested frame size, we need to
  // strip the frame margin off and constrain the client size
  frame.changing.setCoords(frame.changing.left(), frame.changing.top(),
                           frame.changing.right(), frame.changing.bottom());

  unsigned int dw = frame.changing.width(), dh = frame.changing.height(),
    base_width = (client.base_width) ? client.base_width : client.min_width,
    base_height = (client.base_height) ? client.base_height :
                                         client.min_height;

  // constrain
  if (dw < client.min_width) dw = client.min_width;
  if (dh < client.min_height) dh = client.min_height;
  if (dw > client.max_width) dw = client.max_width;
  if (dh > client.max_height) dh = client.max_height;

  assert(dw >= base_width && dh >= base_height);

  if (client.width_inc > 1) {
    dw -= base_width;
    dw /= client.width_inc;
  }
  if (client.height_inc > 1) {
    dh -= base_height;
    dh /= client.height_inc;
  }

  if (pw)
      *pw = dw;

  if (ph)
      *ph = dh;

  if (client.width_inc > 1) {
    dw *= client.width_inc;
    dw += base_width;
  }
  if (client.height_inc > 1) {
    dh *= client.height_inc;
    dh += base_height;
  }

  frame.changing.setSize(dw, dh);

  // add the frame margin back onto frame.changing
  frame.changing.setCoords(frame.changing.left(), frame.changing.top(),
                           frame.changing.right(), frame.changing.bottom());

  // move frame.changing to the specified anchor
  switch (anchor) {
  case TopLeft:
    // nothing to do
    break;

  case TopRight:
    int dx = frame.rect.right() - frame.changing.right();
    frame.changing.setPos(frame.changing.x() + dx, frame.changing.y());
    break;
  }
#endif
}

void* BlackboxWindow::getHWnd(Window w) {
  Atom atom_return;
  int format;
  unsigned long ulfoo, nitems;
  HWND hWnd, *phWnd;

  if ((XGetWindowProperty (blackbox->getXDisplay(), w,
                           blackbox->getWindowsWMNativeHWnd(),
                           0l, 1l, False, XA_INTEGER,
                           &atom_return, &format, &nitems, &ulfoo,
                           (unsigned char **) &phWnd) == Success)
      && phWnd){
    hWnd = *phWnd;
    XFree (phWnd);
    return hWnd;
  } else {
    fprintf(stderr, "void* BlackboxWindow::getHWnd(void) - "
            "XGetWindowProperty failed. %d %d %d %d %d\n",
            atom_return, format, nitems, ulfoo, phWnd);
    return NULL;
  }
}

BWindowGroup::BWindowGroup(Blackbox *b, Window _group)
  : blackbox(b), group(_group) {
  XWindowAttributes wattrib;
  if (! XGetWindowAttributes(blackbox->getXDisplay(), group, &wattrib)) {
    // group window doesn't seem to exist anymore
    delete this;
    return;
  }

  XSelectInput(blackbox->getXDisplay(), group,
               PropertyChangeMask | FocusChangeMask | StructureNotifyMask);

  blackbox->saveGroupSearch(group, this);
}


BWindowGroup::~BWindowGroup(void) {
  blackbox->removeGroupSearch(group);
}


BlackboxWindow *
BWindowGroup::find(BScreen *screen, bool allow_transients) const {
  BlackboxWindow *ret = blackbox->getFocusedWindow();

  // does the focus window match (or any transient_fors)?
  for (; ret; ret = ret->getTransientFor()) {
    if (ret->getScreen() == screen && ret->getGroupWindow() == group &&
        (! ret->isTransient() || allow_transients))
      break;
  }

  if (ret) return ret;

  // the focus window didn't match, look in the group's window list
  BlackboxWindowList::const_iterator it, end = windowList.end();
  for (it = windowList.begin(); it != end; ++it) {
    ret = *it;
    if (ret->getScreen() == screen && ret->getGroupWindow() == group &&
        (! ret->isTransient() || allow_transients))
      break;
  }

  return ret;
}
