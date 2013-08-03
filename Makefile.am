# Makefile.am for Hackedbox
EXTRA_DIST = bootstrap nls doc

AUTOMAKE_OPTIONS = foreign

SUBDIRS = doc nls src

MAINTAINERCLEANFILES = aclocal.m4 config.h.in configure Makefile.in stamp-h.in

uninstall-local:
	-rmdir $(pkgdatadir)

distclean-local:
	rm -f *\~ gmon.out .\#*
