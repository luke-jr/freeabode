lib_LTLIBRARIES = libfreeabode.la

libfreeabode_la_SOURCES = \
	logging.c \
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
	logging.h \
	security.h \
	util.h  \
	$(builddir)/freeabode.pb-c.h
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libfreeabode.pc

dist_noinst_DATA = freeabode.proto
MOSTLYCLEANFILES = freeabode.pb-c.c freeabode.pb-c.h
BUILT_SOURCES = freeabode.pb-c.h

if HAVE_JANSSON
libfreeabode_la_SOURCES += json.c
libfreeabode_include_HEADERS += json.h
libfreeabode_la_CFLAGS += $(JANSSON_CFLAGS)
libfreeabode_la_LIBADD += $(JANSSON_LIBS)
endif


AM_V_PROTOC_C = $(am__v_PROTOC_C_$(V))
am__v_PROTOC_C_ = $(am__v_PROTOC_C_$(AM_DEFAULT_VERBOSITY))
am__v_PROTOC_C_0 = @echo "  PROTOC_C" $*.pb-c.c $*.pb-c.h;
am__v_PROTOC_C_1 =

%.pb-c.c %.pb-c.h: %.proto
	$(AM_V_PROTOC_C)$(PROTOC_C) --proto_path=$(srcdir) --c_out=$(builddir) $^
