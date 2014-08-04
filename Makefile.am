lib_LTLIBRARIES = libfreeabode.la
bin_PROGRAMS = freeabode remctl

libfreeabode_la_SOURCES = \
	security.c \
	util.c
nodist_libfreeabode_la_SOURCES = $(builddir)/freeabode.pb-c.c
libfreeabode_la_CFLAGS = $(LIBSODIUM_CFLAGS) $(LIBZMQ_CFLAGS) $(PROTOBUF_C_CFLAGS)
libfreeabode_la_CFLAGS += -I$(builddir)
libfreeabode_la_LDFLAGS = -version-info $(LIBFREEABODE_SO_VERSION) -no-undefined
libfreeabode_la_LIBADD = $(LIBSODIUM_LIBS) $(LIBZMQ_LIBS) $(PROTOBUF_C_LIBS)
libfreeabode_includedir = $(includedir)/freeabode
libfreeabode_include_HEADERS = \
	bytes.h \
	security.h \
	util.h  \
	$(builddir)/freeabode.pb-c.h
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libfreeabode.pc

freeabode_SOURCES = main.c nest.c nest.h crc.c crc.h
freeabode_LDADD = -lfreeabode

remctl_SOURCES = remctl.c
remctl_LDADD = -lfreeabode

dist_noinst_DATA = freeabode.proto
MOSTLYCLEANFILES = freeabode.pb-c.c freeabode.pb-c.h
BUILT_SOURCES = freeabode.pb-c.h

AM_V_PROTOC_C = $(am__v_PROTOC_C_$(V))
am__v_PROTOC_C_ = $(am__v_PROTOC_C_$(AM_DEFAULT_VERBOSITY))
am__v_PROTOC_C_0 = @echo "  PROTOC_C" $*.pb-c.c $*.pb-c.h;
am__v_PROTOC_C_1 =

%.pb-c.c %.pb-c.h: %.proto
	$(AM_V_PROTOC_C)$(PROTOC_C) --proto_path=$(srcdir) --c_out=$(builddir) $^
