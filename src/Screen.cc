// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Screen.cc for Blackbox - an X11 Window manager
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
#include <X11/Xatom.h>
#include <X11/keysym.h>

// for strcasestr()
#ifndef _GNU_SOURCE
#  define   _GNU_SOURCE
#endif // _GNU_SOURCE

#ifdef HAVE_STDLIB_H
#  include <stdlib.h>
#endif // HAVE_STDLIB_H

#ifdef HAVE_STRING_H
#  include <string.h>
#endif // HAVE_STRING_H

#ifdef    HAVE_CTYPE_H
#  include <ctype.h>
#endif // HAVE_CTYPE_H

#ifdef    HAVE_UNISTD_H
#  include <sys/types.h>
#  include <unistd.h>
#endif // HAVE_UNISTD_H

#ifdef    HAVE_DIRENT_H
#  include <dirent.h>
#endif // HAVE_DIRENT_H

#ifdef    HAVE_LOCALE_H
#  include <locale.h>
#endif // HAVE_LOCALE_H

#ifdef    HAVE_SYS_STAT_H
#  include <sys/stat.h>
#endif // HAVE_SYS_STAT_H

#ifdef    HAVE_STDARG_H
#  include <stdarg.h>
#endif // HAVE_STDARG_H
}

#include <assert.h>

#include <algorithm>
#include <functional>
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
#include <X11/Xlocale.h>

#ifndef   FONT_ELEMENT_SIZE
#define   FONT_ELEMENT_SIZE 50
#endif // FONT_ELEMENT_SIZE


static bool running = True;

static int anotherWMRunning(Display *display, XErrorEvent *) {
  fprintf(stderr, i18n(ScreenSet, ScreenAnotherWMRunning,
          "BScreen::BScreen: an error occured while querying the X server.\n"
          "  another window manager already running on display %s.\n"),
          DisplayString(display));

  running = False;

  return(-1);
}


BScreen::BScreen(Blackbox *bb, unsigned int scrn) : ScreenInfo(bb, scrn) {
  blackbox = bb;

  event_mask = ColormapChangeMask | EnterWindowMask | PropertyChangeMask |
    SubstructureRedirectMask | ButtonPressMask | ButtonReleaseMask;

  XErrorHandler old = XSetErrorHandler((XErrorHandler) anotherWMRunning);
  XSelectInput(getBaseDisplay()->getXDisplay(), getRootWindow(), event_mask);
  XSync(getBaseDisplay()->getXDisplay(), False);
  XSetErrorHandler((XErrorHandler) old);

  managed = running;
  if (! managed) return;

  fprintf(stderr, i18n(ScreenSet, ScreenManagingScreen,
                       "BScreen::BScreen: managing screen %d "
                       "using visual 0x%lx, depth %d\n"),
          getScreenNumber(), XVisualIDFromVisual(getVisual()),
          getDepth());

#ifdef    HAVE_GETPID
  pid_t bpid = getpid();

  XChangeProperty(blackbox->getXDisplay(), getRootWindow(),
                  blackbox->getBlackboxPidAtom(), XA_CARDINAL,
                  sizeof(pid_t) * 8, PropModeReplace,
                  (unsigned char *) &bpid, 1);
#endif // HAVE_GETPID

  int raise_on_click = 0;
  XChangeProperty(blackbox->getXDisplay(), getRootWindow(),
                  blackbox->getWindowsWMRaiseOnClick(),
                  XA_INTEGER, 32,
                  PropModeReplace, (unsigned char *) &raise_on_click, 1);

  int mouse_activate = 0;
  XChangeProperty(blackbox->getXDisplay(), getRootWindow(),
                  blackbox->getWindowsWMMouseActivate(),
                  XA_INTEGER, 32,
                  PropModeReplace, (unsigned char *) &mouse_activate, 1);

  XDefineCursor(blackbox->getXDisplay(), getRootWindow(),
                blackbox->getSessionCursor());

  // start off full screen, top left.
  usableArea.setSize(getWidth(), getHeight());

  root_colormap_installed = True;

  blackbox->load_rc(this);

  LoadStyle();

  XGCValues gcv;
  unsigned long gc_value_mask = GCForeground;
  if (! i18n.multibyte()) gc_value_mask |= GCFont;

  gcv.foreground = WhitePixel(blackbox->getXDisplay(), getScreenNumber())
    ^ BlackPixel(blackbox->getXDisplay(), getScreenNumber());
  gcv.function = GXxor;
  gcv.subwindow_mode = IncludeInferiors;
  opGC = XCreateGC(blackbox->getXDisplay(), getRootWindow(),
                   GCForeground | GCFunction | GCSubwindowMode, &gcv);

  XSetWindowAttributes attrib;
  //unsigned long mask = CWBorderPixel | CWColormap | CWSaveUnder;
  attrib.border_pixel = getBorderColor()->pixel();
  attrib.colormap = getColormap();
  attrib.save_under = True;

  Workspace *wkspc = (Workspace *) 0;
  if (resource.workspaces != 0) {
    for (unsigned int i = 0; i < resource.workspaces; ++i) {
      wkspc = new Workspace(this, workspacesList.size());
      workspacesList.push_back(wkspc);
    }
  } else {
    wkspc = new Workspace(this, workspacesList.size());
    workspacesList.push_back(wkspc);
  }

  current_workspace = workspacesList.front();

  removeWorkspaceNames(); // do not need them any longer

#ifdef ADD_BLOAT
  toolbar = new Toolbar(this);

  slit = new Slit(this);
#endif // ADD_BLOAT

  raiseWindows(0, 0);

  updateAvailableArea();

  changeWorkspaceID(0);

  unsigned int i, j, nchild;
  Window r, p, *children;
  XQueryTree(blackbox->getXDisplay(), getRootWindow(), &r, &p,
             &children, &nchild);

  // preen the window list of all icon windows... for better dockapp support
  for (i = 0; i < nchild; i++) {
    if (children[i] == None) continue;

    XWMHints *wmhints = XGetWMHints(blackbox->getXDisplay(),
                                    children[i]);

    if (wmhints) {
      if ((wmhints->flags & IconWindowHint) &&
          (wmhints->icon_window != children[i])) {
        for (j = 0; j < nchild; j++) {
          if (children[j] == wmhints->icon_window) {
            children[j] = None;
            break;
          }
        }
      }

      XFree(wmhints);
    }
  }

  // manage shown windows
  for (i = 0; i < nchild; ++i) {
    if (children[i] == None || ! blackbox->validateWindow(children[i]))
      continue;

    XWindowAttributes attrib;
    if (XGetWindowAttributes(blackbox->getXDisplay(), children[i], &attrib)) {
      if (attrib.override_redirect) continue;

      if (attrib.map_state != IsUnmapped) {
        manageWindow(children[i]);
      }
    }
  }

  XFree(children);

  // call this again just in case a window we found updates the Strut list
  updateAvailableArea();
}


BScreen::~BScreen(void) {
  if (! managed) return;

  std::for_each(workspacesList.begin(), workspacesList.end(),
                PointerAssassin());

  std::for_each(iconList.begin(), iconList.end(), PointerAssassin());

  std::for_each(netizenList.begin(), netizenList.end(), PointerAssassin());

#ifdef ADD_BLOAT
  delete slit;
  delete toolbar;
#endif // ADD_BLOAT

  XFreeGC(blackbox->getXDisplay(), opGC);
}


void BScreen::removeWorkspaceNames(void) {
  workspaceNames.clear();
}


void BScreen::reconfigure(void) {
  LoadStyle();

  XGCValues gcv;
  unsigned long gc_value_mask = GCForeground;
  if (! i18n.multibyte()) gc_value_mask |= GCFont;

  gcv.foreground = WhitePixel(blackbox->getXDisplay(),
                              getScreenNumber());
  gcv.function = GXinvert;
  gcv.subwindow_mode = IncludeInferiors;
  XChangeGC(blackbox->getXDisplay(), opGC,
            GCForeground | GCFunction | GCSubwindowMode, &gcv);

  raiseWindows(0, 0);

#ifdef ADD_BLOAT
  toolbar->reconfigure();

  slit->reconfigure();
#endif // ADD_BLOAT

  std::for_each(workspacesList.begin(), workspacesList.end(),
                std::mem_fun(&Workspace::reconfigure));

  BlackboxWindowList::iterator iit = iconList.begin();
  for (; iit != iconList.end(); ++iit) {
    BlackboxWindow *bw = *iit;
    if (bw->validateClient())
      bw->reconfigure();
  }
}


void BScreen::LoadStyle(void) {
  // we create the window.frame texture by hand because it exists only to
  // make the code cleaner and is not actually used for display
  BColor color = readDatabaseColor("window.frame.focusColor",
                                   "Window.Frame.FocusColor", "white");

  color = readDatabaseColor("window.frame.unfocusColor",
                            "Window.Frame.UnfocusColor", "white");

  // load toolbar config
#ifdef ADD_BLOAT
  readDatabaseColor("toolbar.label.textColor",
                    "Toolbar.Label.TextColor", "white");
  resource.tstyle.w_text =
    readDatabaseColor("toolbar.windowLabel.textColor",
                      "Toolbar.WindowLabel.TextColor", "white");
  resource.tstyle.c_text =
    readDatabaseColor("toolbar.clock.textColor",
                      "Toolbar.Clock.TextColor", "white");
  resource.tstyle.b_pic =
    readDatabaseColor("toolbar.button.picColor",
                      "Toolbar.Button.PicColor", "black");

  resource.tstyle.justify = LeftJustify;
#endif // ADD_BLOAT

  resource.border_color =
    readDatabaseColor("borderColor", "BorderColor", "black");

  //unsigned int uint_value;

  // load bevel, border and handle widths
  resource.handle_width = 6;
  resource.border_width = 1;
  resource.bevel_width = 3;
  resource.frame_width = resource.bevel_width;
}


void BScreen::addIcon(BlackboxWindow *w) {
  if (! w) return;

  w->setWorkspace(BSENTINEL);
  w->setWindowNumber(iconList.size());

  iconList.push_back(w);

  //const char* title = w->getIconTitle();
}


void BScreen::removeIcon(BlackboxWindow *w) {
  if (! w) return;

  iconList.remove(w);

  BlackboxWindowList::iterator it = iconList.begin(),
    end = iconList.end();
  for (int i = 0; it != end; ++it)
    (*it)->setWindowNumber(i++);
}


BlackboxWindow *BScreen::getIcon(unsigned int index) {
  if (index < iconList.size()) {
    BlackboxWindowList::iterator it = iconList.begin();
    while (index-- > 0)
      it = next_it(it);
    return *it;
  }

  return (BlackboxWindow *) 0;
}


unsigned int BScreen::addWorkspace(void) {
  Workspace *wkspc = new Workspace(this, workspacesList.size());
  workspacesList.push_back(wkspc);

#ifdef ADD_BLOAT
  toolbar->reconfigure();
#endif // ADD_BLOAT

  updateNetizenWorkspaceCount();

  return workspacesList.size();
}


unsigned int BScreen::removeLastWorkspace(void) {
  if (workspacesList.size() == 1)
    return 1;

  Workspace *wkspc = workspacesList.back();

  if (current_workspace->getID() == wkspc->getID())
    changeWorkspaceID(current_workspace->getID() - 1);

  wkspc->removeAll();

  workspacesList.pop_back();
  delete wkspc;

#ifdef ADD_BLOAT
  toolbar->reconfigure();
#endif // ADD_BLOAT

  updateNetizenWorkspaceCount();

  return workspacesList.size();
}


void BScreen::changeWorkspaceID(unsigned int id) {
  if (! current_workspace || id == current_workspace->getID()) return;

  current_workspace->hide();

  current_workspace = getWorkspace(id);

  current_workspace->show();

#ifdef ADD_BLOAT
  toolbar->redrawWorkspaceLabel(True);
#endif // ADD_BLOAT
  
  updateNetizenCurrentWorkspace();
}


void BScreen::manageWindow(Window w) {
  XWMHints *wmhint = XGetWMHints(blackbox->getXDisplay(), w);
  if (wmhint && (wmhint->flags & StateHint) &&
      wmhint->initial_state == WithdrawnState) {
#ifdef ADD_BLOAT
    slit->addClient(w);
#endif // ADD_BLOAT
    return;
  }

  new BlackboxWindow(blackbox, w, this);

  BlackboxWindow *win = blackbox->searchWindow(w);
  if (! win)
    return;

  windowList.push_back(win);

  XMapRequestEvent mre;
  mre.window = w;
  if (blackbox->isStartup()) win->restoreAttributes();
  win->mapRequestEvent(&mre);
}


void BScreen::unmanageWindow(BlackboxWindow *w, bool remap) {
  w->restore(remap);

  if (w->isModal()) w->setModal(False);

  if (w->getWorkspaceNumber() != BSENTINEL &&
      w->getWindowNumber() != BSENTINEL)
    getWorkspace(w->getWorkspaceNumber())->removeWindow(w);
  else if (w->isIconic())
    removeIcon(w);

  windowList.remove(w);

  if (blackbox->getFocusedWindow() == w)
    blackbox->setFocusedWindow((BlackboxWindow *) 0);

  removeNetizen(w->getClientWindow());

  /*
    some managed windows can also be window group controllers.  when
    unmanaging such windows, we should also delete the window group.
  */
  BWindowGroup *group = blackbox->searchGroup(w->getClientWindow());
  delete group;

  delete w;
}


void BScreen::addNetizen(Netizen *n) {
  netizenList.push_back(n);

  n->sendWorkspaceCount();
  n->sendCurrentWorkspace();

  WorkspaceList::iterator it = workspacesList.begin();
  const WorkspaceList::iterator end = workspacesList.end();
  for (; it != end; ++it)
    (*it)->sendWindowList(*n);

  Window f = ((blackbox->getFocusedWindow()) ?
              blackbox->getFocusedWindow()->getClientWindow() : None);
  n->sendWindowFocus(f);
}


void BScreen::removeNetizen(Window w) {
  NetizenList::iterator it = netizenList.begin();
  for (; it != netizenList.end(); ++it) {
    if ((*it)->getWindowID() == w) {
      delete *it;
      netizenList.erase(it);
      break;
    }
  }
}


void BScreen::updateNetizenCurrentWorkspace(void) {
  std::for_each(netizenList.begin(), netizenList.end(),
                std::mem_fun(&Netizen::sendCurrentWorkspace));
}


void BScreen::updateNetizenWorkspaceCount(void) {
  std::for_each(netizenList.begin(), netizenList.end(),
                std::mem_fun(&Netizen::sendWorkspaceCount));
}


void BScreen::updateNetizenWindowFocus(void) {
  Window f = ((blackbox->getFocusedWindow()) ?
              blackbox->getFocusedWindow()->getClientWindow() : None);
  NetizenList::iterator it = netizenList.begin();
  for (; it != netizenList.end(); ++it)
    (*it)->sendWindowFocus(f);
}


void BScreen::updateNetizenWindowAdd(Window w, unsigned long p) {
  NetizenList::iterator it = netizenList.begin();
  for (; it != netizenList.end(); ++it) {
    (*it)->sendWindowAdd(w, p);
  }
}


void BScreen::updateNetizenWindowDel(Window w) {
  NetizenList::iterator it = netizenList.begin();
  for (; it != netizenList.end(); ++it)
    (*it)->sendWindowDel(w);
}


void BScreen::updateNetizenWindowRaise(Window w) {
  NetizenList::iterator it = netizenList.begin();
  for (; it != netizenList.end(); ++it)
    (*it)->sendWindowRaise(w);
}


void BScreen::updateNetizenWindowLower(Window w) {
  NetizenList::iterator it = netizenList.begin();
  for (; it != netizenList.end(); ++it)
    (*it)->sendWindowLower(w);
}


void BScreen::updateNetizenConfigNotify(XEvent *e) {
  NetizenList::iterator it = netizenList.begin();
  for (; it != netizenList.end(); ++it)
    (*it)->sendConfigNotify(e);
}


void BScreen::raiseWindows(Window *workspace_stack, unsigned int num) {
  // the 13 represents the number of blackbox windows such as menus
  Window *session_stack = new
    Window[(num + workspacesList.size() + 13)];
  unsigned int i = 0, k = num;

  WorkspaceList::iterator wit = workspacesList.begin();

#ifdef ADD_BLOAT
  if (toolbar->isOnTop())
    *(session_stack + i++) = toolbar->getWindowID();

  if (slit->isOnTop())
    *(session_stack + i++) = slit->getWindowID();
#endif // ADD_BLOAT

  while (k--)
    *(session_stack + i++) = *(workspace_stack + k);

  XRaiseWindow(blackbox->getXDisplay(), session_stack[0]);
  XRestackWindows(blackbox->getXDisplay(), session_stack, i);

  delete [] session_stack;
}


void BScreen::addWorkspaceName(const string& name) {
  workspaceNames.push_back(name);
}


/*
 * I would love to kill this function and the accompanying workspaceNames
 * list.  However, we have a chicken and egg situation.  The names are read
 * in during load_rc() which happens before the workspaces are created.
 * The current solution is to read the names into a list, then use the list
 * later for constructing the workspaces.  It is only used during initial
 * BScreen creation.
 */
const string BScreen::getNameOfWorkspace(unsigned int id) {
  if (id < workspaceNames.size())
    return workspaceNames[id];
  return string("");
}


void BScreen::reassociateWindow(BlackboxWindow *w, unsigned int wkspc_id,
                                bool /*ignore_sticky*/) {
  if (! w) return;

  if (wkspc_id == BSENTINEL)
    wkspc_id = current_workspace->getID();

  if (w->getWorkspaceNumber() == wkspc_id)
    return;

  if (w->isIconic()) {
    removeIcon(w);
    getWorkspace(wkspc_id)->addWindow(w);
  } else {
    getWorkspace(w->getWorkspaceNumber())->removeWindow(w);
    getWorkspace(wkspc_id)->addWindow(w);
  }
}


void BScreen::propagateWindowName(const BlackboxWindow *bw) {
  if (bw->isIconic()) {
  }
  else {
#ifdef ADD_BLOAT
    if (blackbox->getFocusedWindow() == bw)
      toolbar->redrawWindowLabel(True);
#endif // ADD_BLOAT
  }
}


void BScreen::nextFocus(void) {
  BlackboxWindow *focused = blackbox->getFocusedWindow(),
    *next = focused;

  if (focused) {
    // if window is not on this screen, ignore it
    if (focused->getScreen()->getScreenNumber() != getScreenNumber())
      focused = (BlackboxWindow*) 0;
  }

  if (focused && current_workspace->getCount() > 1) {
    // next is the next window to recieve focus, current is a place holder
    BlackboxWindow *current;
    do {
      current = next;
      next = current_workspace->getNextWindowInList(current);
    } while(!next->setInputFocus() && next != focused);

    if (next != focused)
      current_workspace->raiseWindow(next);
  } else if (current_workspace->getCount() >= 1) {
    next = current_workspace->getTopWindowOnStack();

    current_workspace->raiseWindow(next);
    next->setInputFocus();
  }
}


void BScreen::prevFocus(void) {
  BlackboxWindow *focused = blackbox->getFocusedWindow(),
    *next = focused;

  if (focused) {
    // if window is not on this screen, ignore it
    if (focused->getScreen()->getScreenNumber() != getScreenNumber())
      focused = (BlackboxWindow*) 0;
  }

  if (focused && current_workspace->getCount() > 1) {
    // next is the next window to recieve focus, current is a place holder
    BlackboxWindow *current;
    do {
      current = next;
      next = current_workspace->getPrevWindowInList(current);
    } while(!next->setInputFocus() && next != focused);

    if (next != focused)
      current_workspace->raiseWindow(next);
  } else if (current_workspace->getCount() >= 1) {
    next = current_workspace->getTopWindowOnStack();

    current_workspace->raiseWindow(next);
    next->setInputFocus();
  }
}


void BScreen::raiseFocus(void) {
  BlackboxWindow *focused = blackbox->getFocusedWindow();
  if (! focused)
    return;

  // if on this Screen, raise it
  if (focused->getScreen()->getScreenNumber() == getScreenNumber()) {
    Workspace *workspace = getWorkspace(focused->getWorkspaceNumber());
    workspace->raiseWindow(focused);
  }
}


static
size_t string_within(char begin, char end,
                     const char *input, size_t start_at, size_t length,
                     char *output) {
  bool parse = False;
  size_t index = 0;
  size_t i = start_at;
  for (; i < length; ++i) {
    if (input[i] == begin) {
      parse = True;
    } else if (input[i] == end) {
      break;
    } else if (parse) {
      if (input[i] == '\\' && i < length - 1) i++;
      output[index++] = input[i];
    } 
  }

  if (parse)
    output[index] = '\0';
  else
    output[0] = '\0';

  return i;
}


void BScreen::shutdown(void) {
  XSelectInput(blackbox->getXDisplay(), getRootWindow(), NoEventMask);
  XSync(blackbox->getXDisplay(), False);

  while(! windowList.empty())
    unmanageWindow(windowList.front(), True);

#ifdef ADD_BLOAT
 slit->shutdown();
#endif // ADD_BLOAT
}


void BScreen::showPosition(int /*x*/, int /*y*/) {
}


void BScreen::addStrut(Strut *strut) {
  strutList.push_back(strut);
}


void BScreen::removeStrut(Strut *strut) {
  strutList.remove(strut);
}


const Rect& BScreen::availableArea(void) const {
  if (doFullMax())
    return getRect(); // return the full screen
  return usableArea;
}


void BScreen::updateAvailableArea(void) {
  Rect old_area = usableArea;
  usableArea = getRect(); // reset to full screen

  /* these values represent offsets from the screen edge
   * we look for the biggest offset on each edge and then apply them
   * all at once
   * do not be confused by the similarity to the names of Rect's members
   */
  unsigned int current_left = 0, current_right = 0, current_top = 0,
    current_bottom = 0;

  StrutList::const_iterator it = strutList.begin(), end = strutList.end();

  for(; it != end; ++it) {
    Strut *strut = *it;
    if (strut->left > current_left)
      current_left = strut->left;
    if (strut->top > current_top)
      current_top = strut->top;
    if (strut->right > current_right)
      current_right = strut->right;
    if (strut->bottom > current_bottom)
      current_bottom = strut->bottom;
  }

  usableArea.setPos(current_left, current_top);
  usableArea.setSize(usableArea.width() - (current_left + current_right),
                     usableArea.height() - (current_top + current_bottom));

  if (old_area != usableArea) {
    BlackboxWindowList::iterator it = windowList.begin(),
      end = windowList.end();
    for (; it != end; ++it)
      if ((*it)->isMaximized()) (*it)->remaximize();
  }
}


Workspace* BScreen::getWorkspace(unsigned int index) {
  assert(index < workspacesList.size());
  return workspacesList[index];
}


void BScreen::buttonPressEvent(const XButtonEvent *xbutton) {
  if (xbutton->button == 1) {
  } else if (xbutton->button == 2) {
  } else if (xbutton->button == 3) {
  }
}


void BScreen::toggleFocusModel(FocusModel model) {
  std::for_each(windowList.begin(), windowList.end(),
                std::mem_fun(&BlackboxWindow::ungrabButtons));
  
  if (model == SloppyFocus) {
    saveSloppyFocus(True);
  } else {
    saveSloppyFocus(False);
    saveAutoRaise(False);
    saveClickRaise(False);
  }

  std::for_each(windowList.begin(), windowList.end(),
                std::mem_fun(&BlackboxWindow::grabButtons));
}


BColor BScreen::readDatabaseColor(const string &/*rname*/, const string &/*rclass*/,
				  const string &default_color) {
  BColor color;

  color = BColor(default_color, getBaseDisplay(), getScreenNumber());
  return color;
}


#ifndef    HAVE_STRCASESTR
static const char * strcasestr(const char *str, const char *ptn) {
  const char *s2, *p2;
  for(; *str; str++) {
    for(s2=str,p2=ptn; ; s2++,p2++) {
      if (!*p2) return str;
      if (toupper(*s2) != toupper(*p2)) break;
    }
  }
  return NULL;
}
#endif // HAVE_STRCASESTR
