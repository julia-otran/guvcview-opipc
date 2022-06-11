## Process this file with automake to produce Makefile.in

#AM_CFLAGS =

bin_PROGRAMS = guvcview

guvcview_SOURCES = guvcview.c \
				   video_capture.c \
				   core_io.c \
				   options.c \
				   config.c \
				   controls.c \
				   cec_controls.c

guvcview_CFLAGS = $(PTHREAD_CFLAGS) \
		  $(GUVCVIEW_CFLAGS) \
		  -D_REENTRANT\
		  -D_FILE_OFFSET_BITS=64\
		  -Wall\
		  -DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
		  -DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
		  -DPACKAGE_DATA_DIR=\""$(datadir)"\" \
		  $(DEPS_CFLAGS) $(EXTRA_CFLAGS) $(PTHREAD_CFLAGS) \
		  $(EXTRA_CFLAGS) -I$(top_srcdir) -I$(top_srcdir)/includes \
		  -I$(top_srcdir)/gview_v4l2core

guvcview_LDFLAGS = $(LIBINTL) --verbose -L/usr/local/lib -pthread -lm -lz -lm -pthread -lX11 -lm -lX11

guvcview_LDADD = ../gview_v4l2core/$(GVIEWV4L2CORE_LIBRARY_NAME).la \
				$(PTHREAD_LIBS) \
				$(GUVCVIEW_LIBS) \
				-lm
moc_%.cpp: %.hpp
	$(MOC) $< -o $@

localedir = $(datadir)/locale
DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@



