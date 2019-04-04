AM_CFLAGS  = -I$(top_builddir)/include -I$(top_builddir)/lib -I$(top_builddir)/libmetrics

if STATIC_BUILD
noinst_LTLIBRARIES    = libmodibmpower.la
libmodibmpower_la_SOURCES = mod_ibmpower.c 
else
pkglib_LTLIBRARIES    = modibmpower.la
modibmpower_la_SOURCES = mod_ibmpower.c 
modibmpower_la_LDFLAGS = -module -avoid-version
modibmpower_la_LIBADD = $(top_builddir)/libmetrics/libmetrics.la

EXTRA_DIST = ../conf.d/ibmpower.conf
endif

INCLUDES = @APR_INCLUDES@

