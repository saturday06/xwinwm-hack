# src/Makefile.am for Blackbox - an X11 Window manager
# Copyright (c) 1997 - 2000 Brad Hughes (bhughes@tcac.net)
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the 
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in 
# all copies or substantial portions of the Software. 
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL 
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
# DEALINGS IN THE SOFTWARE.
EXTRA_DIST=$(srcdir)/*.hh $(srcdir)/*.in

AM_CPPFLAGS= @CPPFLAGS@ @SHAPE@ @ORDEREDPSEUDO@ \
@DEBUG@ @NLS@ @TIMEDCACHE@ \
-DLOCALEPATH=\"$(pkgdatadir)/nls\" \
-DDEFAULTSTYLE=\"$(DEFAULT_STYLE)\" \
-DX_LOCALE

bin_PROGRAMS= xwinwm

xwinwm_SOURCES= BaseDisplay.cc Color.cc GCCache.cc Netizen.cc Screen.cc \
Timer.cc Util.cc Window.cc Workspace.cc blackbox.cc i18n.cc main.cc

MAINTAINERCLEANFILES= Makefile.in

distclean-local:
	rm -f *\~ *.orig *.rej .\#*

# local dependencies

BaseDisplay.o: BaseDisplay.cc ../config.h i18n.hh \
 ../nls/blackbox-nls.hh BaseDisplay.hh Timer.hh GCCache.hh Color.hh \
 Util.hh
Color.o: Color.cc ../config.h Color.hh BaseDisplay.hh Timer.hh
GCCache.o: GCCache.cc ../config.h GCCache.hh BaseDisplay.hh Timer.hh \
 Color.hh Util.hh
Netizen.o: Netizen.cc ../config.h Netizen.hh Screen.hh Color.hh \
 Util.hh Timer.hh Workspace.hh blackbox.hh i18n.hh \
 ../nls/blackbox-nls.hh BaseDisplay.hh
Screen.o: Screen.cc ../config.h i18n.hh ../nls/blackbox-nls.hh \
 blackbox.hh BaseDisplay.hh Timer.hh GCCache.hh Color.hh Screen.hh Util.hh \
 Netizen.hh Workspace.hh Window.hh
Timer.o: Timer.cc ../config.h BaseDisplay.hh Timer.hh Util.hh
Util.o: Util.cc ../config.h Util.hh
Window.o: Window.cc ../config.h i18n.hh ../nls/blackbox-nls.hh \
 blackbox.hh BaseDisplay.hh Timer.hh GCCache.hh Color.hh Screen.hh Util.hh \
 Netizen.hh Workspace.hh Window.hh
Workspace.o: Workspace.cc ../config.h i18n.hh ../nls/blackbox-nls.hh \
 blackbox.hh BaseDisplay.hh Timer.hh Netizen.hh Screen.hh Color.hh Util.hh \
 Workspace.hh Window.hh
blackbox.o: blackbox.cc ../config.h i18n.hh ../nls/blackbox-nls.hh \
 blackbox.hh BaseDisplay.hh Timer.hh GCCache.hh Color.hh Screen.hh Util.hh \
 Netizen.hh Workspace.hh Window.hh
i18n.o: i18n.cc ../config.h i18n.hh ../nls/blackbox-nls.hh
main.o: main.cc ../version.h ../config.h i18n.hh \
 ../nls/blackbox-nls.hh blackbox.hh BaseDisplay.hh Timer.hh
