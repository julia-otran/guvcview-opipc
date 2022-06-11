dnl Process this file with autoconf to produce a configure script.

# Save the original $CFLAGS so we can distinguish whether the user set those
# in the environment, or whether autoconf added -O and -g options:
ORIGINAL_CFLAGS="$CFLAGS"
ORIGINAL_CPPFLAGS="$CPPFLAGS"

dnl --------------------------------
dnl Initialization macros (sets PACKAGE and VERSION)
dnl --------------------------------
m4_define([guvcview_major], [2])
m4_define([guvcview_minor], [0])
m4_define([guvcview_micro], [5])

m4_define([guvcview_version],
          [guvcview_major.guvcview_minor.guvcview_micro])

AC_INIT([guvcview], [guvcview_version], [http://guvcview.sourceforge.net/])

PACKAGE_RELEASE=2
AC_SUBST(PACKAGE_RELEASE)
AC_DEFINE_UNQUOTED(PACKAGE_RELEASE,"$PACKAGE_RELEASE", 2)

dnl ------------------------------------------------
dnl Pretty output
dnl ------------------------------------------------
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS(config.h)
AM_MAINTAINER_MODE(disable)

dnl -----------------------------------------------
dnl Package release
dnl -----------------------------------------------

# autoconf 2.5x defaults to no cache file; we need the cache file's information
# for building the config page.  But start with it empty to avoid confusion by
# people who don't do a "make distclean" after applying patches.
cache_file=config.cache
rm -f config.cache; touch config.cache

AC_PREFIX_DEFAULT(/usr/local)

# check for clang first
# gcc will still be used if clang not installed 

AC_PROG_CC([clang gcc cc])
AM_PROG_CC_C_O
AC_PROG_CXX([clang++ g++ cc++ cxx])
AC_PROG_CXX_C_O
AC_HEADER_STDC
AC_PROG_LIBTOOL
# enable Large File Support extension (LFS)
AC_SYS_LARGEFILE
AM_SANITY_CHECK

dnl --------------------------------------------------------------------------
dnl set pthread_libs and pthread_cflags
dnl --------------------------------------------------------------------------
dnl ACX_PTHREAD([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])

ACX_PTHREAD()

AC_SUBST(CFLAGS)

dnl --------------------------------------------------------------------------
dnl Set gettext package name
dnl --------------------------------------------------------------------------

GETTEXT_PACKAGE_V4L2CORE=gview_v4l2core
AC_SUBST(GETTEXT_PACKAGE_V4L2CORE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE_V4L2CORE,"$GETTEXT_PACKAGE_V4L2CORE", [gview_v4l2core])

GETTEXT_PACKAGE=guvcview
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [guvcview])

IT_PROG_INTLTOOL([0.40])
IT_PO_SUBDIR(po/gview_v4l2core)

dnl --------------------------------------------------------------------------
dnl check for guvcview dependencies
dnl --------------------------------------------------------------------------

PKG_CHECK_MODULES(GUVCVIEW, [json-c])
GUVCVIEW_CFLAGS+=" -march=armv7-a+neon-vfpv4 -mfpu=neon-vfpv4 "
AC_SUBST(GUVCVIEW_CFLAGS)
AC_SUBST(GUVCVIEW_LIBS)

dnl --------------------------------------------------------------------------
dnl Languages supported by guvcview.
dnl --------------------------------------------------------------------------

ALL_LINGUAS="bg bs cs da de en_AU es eu fo fr gl he hr it ja lv nl pl pt pt_BR ru si sr tr uk zh_TW"
AM_GLIB_GNU_GETTEXT([external])

dnl -----------------------------------------------
dnl  check for some required headers
dnl -----------------------------------------------
AC_STDC_HEADERS
AC_HAVE_HEADERS(math.h)
AC_CHECK_LIB([m],[exp],[AC_DEFINE([HAVE_EXP],[1],[libm includes exp])])
AC_CHECK_LIB([m],[roundf],[AC_DEFINE([HAVE_ROUNDF],[1],[libm includes roundf])])
AC_CHECK_LIB([m],[pow],[AC_DEFINE([HAVE_POW],[1],[libm includes pow])])

dnl -----------------------------------------------
dnl libgviewv4l2core name and version number
dnl -----------------------------------------------

GVIEWV4L2CORE_LIBRARY_NAME=libgviewv4l2core
AC_SUBST(GVIEWV4L2CORE_LIBRARY_NAME)
GVIEWV4L2CORE_LD_NAME=gviewv4l2core
AC_SUBST(GVIEWV4L2CORE_LD_NAME)

#release versioning
GVIEWV4L2CORE_CURRENT_VERSION=2
GVIEWV4L2CORE_REVISION_VERSION=0
GVIEWV4L2CORE_AGE_VERSION=0

#API version (SONAME)
GVIEWV4L2CORE_API_VERSION=$GVIEWV4L2CORE_CURRENT_VERSION.$GVIEWV4L2CORE_REVISION_VERSION
AC_SUBST(GVIEWV4L2CORE_API_VERSION)

#shared library versioning
GVIEWV4L2CORE_LIBRARY_VERSION=$GVIEWV4L2CORE_CURRENT_VERSION:$GVIEWV4L2CORE_REVISION_VERSION:$GVIEWV4L2CORE_AGE_VERSION
#
#             current:revision:age
#                |       |     |
#                |       |     +- increment if interfaces have been added
#                |       |        set to zero if interfaces have been removed
#                                  or changed
#                |       +- increment if source code has changed
#                |          set to zero if current is incremented
#                +- increment if interfaces have been added, removed or changed

AC_SUBST(GVIEWV4L2CORE_LIBRARY_VERSION)

dnl --------------------------------------------------------------------------
dnl check for libgviewv4l2core dependencies
dnl --------------------------------------------------------------------------

PKG_CHECK_MODULES(GVIEWV4L2CORE, [libv4l2 libudev libusb-1.0 libdrm])

GVIEWV4L2CORE_CFLAGS+=" -march=armv7-a+neon-vfpv4 -mfpu=neon-vfpv4 " 

AC_SUBST(GVIEWV4L2CORE_CFLAGS)
AC_SUBST(GVIEWV4L2CORE_LIBS)

dnl restore CCPFLAGS to avoid redefinitions in debian build tree
dnl CPPFLAGS="${ORIGINAL_CPPFLAGS}"

dnl --------------------------------------------------------------------------
dnl check for guvcview dependencies
dnl --------------------------------------------------------------------------

AC_CHECK_FUNCS([fpathconf dirfd])

dnl --------------------------------------------------------------------------
dnl Check if the processor stores words with the most significant byte first
dnl (like Motorola and SPARC, unlike Intel and VAX).
dnl --------------------------------------------------------------------------

AC_C_BIGENDIAN

dnl -----------------------------------------------
dnl Generates Makefiles, configuration files and scripts
dnl -----------------------------------------------

AC_CONFIG_FILES([
	Makefile
    pkgconfig/libgviewv4l2core.pc
    gview_v4l2core/Makefile
    guvcview/Makefile
    data/Makefile
    data/icons/Makefile
    data/guvcview.desktop.in
    data/guvcview.appdata.xml.in
    data/guvcview.in
    po/Makefile.in
    po/gview_v4l2core/Makefile.in
])
AC_OUTPUT

dnl Output the results
AC_MSG_NOTICE([

  guvcview $VERSION
  ----------------------

  Prefix           : ${prefix}
  mjpg decoder     : Cedar

])

