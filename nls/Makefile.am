# nls/Makefile.am for Blackbox - an X11 Window Manager

NLSTEST = @NLS@
SUBDIRS = C
#	  da_DK de_DE es_AR es_ES et_EE fr_FR hu_HU it_IT ja_JP ko_KR nl_NL \
#	  no_NO pl_PL pt_BR ro_RO ru_RU sk_SK sl_SI sv_SE tr_TR uk_UA \
#	  zh_CN zh_TW
MAINTAINERCLEANFILES = Makefile.in blackbox-nls.hh
DISTCLEANFILES = blackbox-nls.hh

uninstall-local:
	@if test x$(NLSTEST) = "x-DNLS"; then \
		rmdir $(DESTDIR)$(pkgdatadir)/nls; \
	fi
distclean-local:
	rm -f *\~ .\#*
