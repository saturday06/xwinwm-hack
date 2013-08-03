dnl configure.in for XWinWM
dnl Initialize autoconf and automake
AC_INIT(xwinwm, 0.0.5)
AC_CONFIG_SRCDIR(src/Window.cc)
AM_INIT_AUTOMAKE([dist-bzip2])

dnl Determine default prefix
test x$prefix = "xNONE" && prefix="$ac_default_prefix"

dnl Locate required external software
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL

AC_CHECK_PROGS(regex_cmd, sed)
if test x$regex_cmd = "x"; then
  AC_MSG_ERROR([error. sed is required to build the default menu file.])
fi

dnl Check for system header files
AC_CHECK_HEADERS(ctype.h dirent.h fcntl.h libgen.h locale.h nl_types.h process.h signal.h stdarg.h stdio.h stdlib.h string.h time.h unistd.h sys/param.h sys/select.h sys/signal.h sys/stat.h sys/time.h sys/types.h sys/wait.h)
AC_HEADER_TIME

dnl Check for existance of basename(), setlocale() and strftime()
AC_CHECK_FUNCS(basename, , AC_CHECK_LIB(gen, basename,
			  AC_DEFINE(HAVE_BASENAME) LIBS="$LIBS -lgen"))
AC_CHECK_FUNCS(getpid setlocale sigaction strftime strcasestr snprintf vsnprintf catopen catgets catclose)
AC_CHECK_LIB(nsl, t_open, LIBS="$LIBS -lnsl")
AC_CHECK_LIB(socket, socket, LIBS="$LIBS -lsocket")

dnl Check for X headers and libraries
AC_PATH_X
AC_PATH_XTRA

test x$no_x = "xyes" && AC_MSG_ERROR([Hackedbox requires the X Window System libraries and headers.])

test x$x_includes = "x" && x_includes="/usr/include"
test x$x_libraries = "x" && x_libraries="/usr/lib"

CFLAGS="$CFLAGS $X_CFLAGS"
CXXFLAGS="$CXXFLAGS $X_CFLAGS"
LIBS="$LIBS $X_LIBS"
LDFLAGS="$LDFLAGS $LIBS $X_PRE_LIBS"

AC_MSG_CHECKING(for GCC)
if test x$GCC = "xyes"; then
  AC_MSG_RESULT([yes])
  CXXFLAGS="${CXXFLAGS} -Wall -W -pedantic"
else
  AC_MSG_RESULT([no, trying other compilers])
  AC_MSG_CHECKING(for MIPSpro)
  mips_pro_ver=`$CC -version 2>&1 | grep -i mipspro | cut -f4 -d ' '`
  if test "x$mips_pro_ver" = "x"; then
    AC_MSG_RESULT([no])
  else
    AC_MSG_RESULT([yes, version $mips_pro_ver.])
    AC_MSG_CHECKING(for -LANG:std in CXXFLAGS)
    lang_std_not_set=`echo $CXXFLAGS | grep "\-LANG:std"`
    if test "x$lang_std_not_set" = "x"; then
      AC_MSG_RESULT([not set, setting.])
      CXXFLAGS="${CXXFLAGS} -LANG:std"
    else
      AC_MSG_RESULT([already set.])
    fi
  fi 
fi

dnl Check for required functions in -lX11
AC_CHECK_LIB(X11, XOpenDisplay,
  LIBS="$LIBS -lX11",
  AC_MSG_ERROR([Could not find XOpenDisplay in -lX11.])
)

LIBS="$LIBS $X_EXTRA_LIBS"

Xext_lib=""

dnl Check for XShape extension support and proper library files.
SHAPE=""
AC_MSG_CHECKING([whether to build support for the XShape extension])
AC_ARG_ENABLE(
  shape, 
  [  --enable-shape          enable support of the XShape extension [default=yes]])
  : ${enableval="yes"}
if test x$enableval = "xyes"; then
  AC_MSG_RESULT([yes])
  AC_CHECK_LIB(Xext, XShapeCombineShape,
    AC_MSG_CHECKING([for X11/extensions/shape.h])
    AC_TRY_LINK(
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
, long foo = ShapeSet,
      AC_MSG_RESULT([yes])
      SHAPE="-DSHAPE"; Xext_lib="-lXext",
      AC_MSG_RESULT([no])
    )
  )
else
  AC_MSG_RESULT([no])
fi
AC_SUBST(SHAPE)


Xwindowswm_lib=""

dnl Check for WindowsWM extension support and proper library files.
WINDOWSWM=""
AC_MSG_CHECKING([whether to build support for the WindowsWM extension])
AC_ARG_ENABLE(
  windowswm, 
  [  --enable-windows-wm          enable support of the WindowsWM extension [default=yes]])
  : ${enableval="yes"}
if test x$enableval = "xyes"; then
  AC_MSG_RESULT([yes])
  AC_CHECK_LIB(WindowsWM, XWindowsWMSelectInput,
    AC_MSG_CHECKING([for X11/extensions/windowswm.h])
    AC_TRY_LINK(
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/windowswm.h>
      ,,
      AC_MSG_RESULT([yes])
      WINDOWSWM="-DWINDOWSWM"; Xwindowswm_lib="-lWindowsWM",
      AC_MSG_RESULT([no])
    ),,
    -lX11 -lXext
  )
else
  AC_MSG_RESULT([no])
fi
AC_SUBST(WINDOWSWM)

LIBS="$Xwindowswm_lib $LIBS $Xext_lib"

dnl Check for ordered 8bpp dithering
ORDEREDPSEUDO=""
AC_MSG_CHECKING([whether to include Pseudocolor ordered dithering code])
AC_ARG_ENABLE(ordered-pseudo,
[  --enable-ordered-pseudo include code for ordered pseudocolor (8bpp)
                          dithering [default=no]],
  if test x$enableval = "xyes"; then
    AC_MSG_RESULT([yes])
    ORDEREDPSEUDO="-DORDEREDPSEUDO" 
  else
    AC_MSG_RESULT([no]) 
  fi,
  AC_MSG_RESULT([no])
)
AC_SUBST(ORDEREDPSEUDO)

dnl Check whether to include debugging code
DEBUG=""
AC_MSG_CHECKING([whether to include verbose debugging code])
AC_ARG_ENABLE(debug,
  [  --enable-debug          include verbose debugging code [default=no]],
  if test x$enableval = "xyes"; then
    AC_MSG_RESULT([yes])
    if test x$GCC = "xyes"; then
       DEBUG="-DDEBUG -fno-inline -g"
    else
       DEBUG="-DDEBUG"
    fi
  else
    AC_MSG_RESULT([no])
    DEBUG="-DNDEBUG" 
fi,
  AC_MSG_RESULT([no])
  DEBUG="-DNDEBUG" 
)
AC_SUBST(DEBUG)

dnl Check whether to include natural language support (i18n)
NLS=""
AC_MSG_CHECKING([whether to include NLS support])
AC_ARG_ENABLE(nls,
  [  --enable-nls            include natural language support [default=yes]],
  if test x$enableval = "xyes"; then
    AC_MSG_RESULT([yes])
    NLS="-DNLS"
  else
    AC_MSG_RESULT([no])
  fi,
  AC_MSG_RESULT([yes])
  NLS="-DNLS"
)
AC_SUBST(NLS)

AC_CHECK_LIB(xpg4, setlocale, LIBS="$LIBS -lxpg4")

AC_CHECK_PROGS(gencat_cmd, gencat)
if test x$gencat_cmd = "x"; then
  NLS=""
fi


dnl Check for new timed pixmap cache
TIMEDCACHE=""
AC_MSG_CHECKING([whether to use the new timed pixmap cache])
AC_ARG_ENABLE(
  timed-cache,
[  --enable-timed-cache    use new timed pixmap cache [default=yes]],
  if test x$enableval = "xyes"; then
    AC_MSG_RESULT([yes])
    TIMEDCACHE="-DTIMEDCACHE"
  else
    AC_MSG_RESULT([no])
  fi,
  AC_MSG_RESULT([yes])
  TIMEDCACHE="-DTIMEDCACHE"
)
AC_SUBST(TIMEDCACHE)

dnl Determine the return type of signal handlers
AC_TYPE_SIGNAL

dnl Determine if maintainer portions of the Makefiles should be included.
AM_MAINTAINER_MODE

dnl Output files
AM_CONFIG_HEADER(config.h)
AC_CONFIG_FILES(Makefile
src/Makefile
src/blackbox.cc
doc/xwinwm.1
doc/Makefile
doc/ja_JP/Makefile
nls/Makefile
nls/C/Makefile
dnl nls/da_DK/Makefile
dnl nls/de_DE/Makefile
dnl nls/es_AR/Makefile
dnl nls/es_ES/Makefile
dnl nls/et_EE/Makefile
dnl nls/fr_FR/Makefile
dnl nls/hu_HU/Makefile
dnl nls/it_IT/Makefile
dnl nls/ja_JP/Makefile
dnl nls/ko_KR/Makefile
dnl nls/nl_NL/Makefile
dnl nls/no_NO/Makefile
dnl nls/pl_PL/Makefile
dnl nls/pt_BR/Makefile
dnl nls/ro_RO/Makefile
dnl nls/ru_RU/Makefile
dnl nls/sk_SK/Makefile
dnl nls/sl_SI/Makefile
dnl nls/sv_SE/Makefile
dnl nls/tr_TR/Makefile
dnl nls/uk_UA/Makefile
dnl nls/zh_CN/Makefile
dnl nls/zh_TW/Makefile
version.h)
AC_OUTPUT

# Print results
AC_MSG_RESULT([])
AC_MSG_RESULT([***************************************************])
AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE version $VERSION configured successfully.])
AC_MSG_RESULT([])
AC_MSG_RESULT([Using '$prefix' for installation.])
AC_MSG_RESULT([Using '$CXX' for C++ compiler.])
AC_MSG_RESULT([Building with '$CXXFLAGS' for C++ compiler flags.])
AC_MSG_RESULT([Building with '$LIBS' for linker flags.])
AC_MSG_RESULT([])
AC_MSG_RESULT([***************************************************])
