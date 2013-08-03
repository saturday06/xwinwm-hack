# doc/Makefile.am for XWinWM

CLEANFILES = xwinwm.1
MAINTAINERCLEANFILES = Makefile.in
man_MANS = xwinwm.1
SUBDIRS = ja_JP

DEFAULT_MENU=$(pkgdatadir)/menu

distclean-local:
	rm -f *\~ .\#*

xwinwm.1: xwinwm.1.in
	@regex_cmd@ -e "s,@defaultmenu@,$(DEFAULT_MENU)," \
		-e "s,@pkgdatadir@,$(pkgdatadir)," \
		-e "s,@version@,$(VERSION)," \
		 @srcdir@/xwinwm.1.in > xwinwm.1
