# doc/<locale>/Makefile.am for XWinWM - an X11 Window manager

# what locale is this
locale = ja_JP

# where to install
locale_mandir = $(DESTDIR)$(mandir)/$(locale)/man1

# what to install
manfiles = xwinwm.1

# which files need processing during 'make'
all-local: xwinwm.1

CLEANFILES = xwinwm.1
MAINTAINERCLEANFILES = Makefile.in

NLSTEST = @NLS@
DEFAULT_MENU=$(pkgdatadir)/menu

distclean-local:
	rm -f *\~ .\#*

xwinwm.1: xwinwm.1.in
	@regex_cmd@ -e "s,@defaultmenu@,$(DEFAULT_MENU)," \
		-e "s,@pkgdatadir@,$(pkgdatadir)," \
		-e "s,@version@,$(VERSION)," \
		@srcdir@/xwinwm.1.in > xwinwm.1

install-data-local: all-local
	@if test x$(NLSTEST) = "x-DNLS"; then \
		echo "Installing manual pages for $(locale)"; \
		$(mkinstalldirs) $(locale_mandir); \
		$(INSTALL_DATA) $(manfiles) $(locale_mandir); \
	fi

uninstall-local:
	@if test x$(NLSTEST) = "x-DNLS"; then \
		for i in $(manfiles); do \
		rm -f $(locale_mandir)/`basename $$i`; \
		done; \
	fi
