bin_PROGRAMS = freeabode
freeabode_SOURCES = main.c nest.c nest.h bytes.h crc.c crc.h security.c security.h util.c util.h
freeabode_CFLAGS = $(LIBSODIUM_CFLAGS) $(LIBZMQ_CFLAGS) $(PROTOBUF_C_CFLAGS)
freeabode_LDADD = $(LIBSODIUM_LIBS) $(LIBZMQ_LIBS) $(PROTOBUF_C_LIBS)

nodist_freeabode_SOURCES = $(builddir)/freeabode.pb-c.c $(builddir)/freeabode.pb-c.h
dist_noinst_DATA = freeabode.proto
MOSTLYCLEANFILES = freeabode.pb-c.c freeabode.pb-c.h
BUILT_SOURCES = freeabode.pb-c.h
freeabode_CFLAGS += -I$(builddir)

AM_V_PROTOC_C = $(am__v_PROTOC_C_$(V))
am__v_PROTOC_C_ = $(am__v_PROTOC_C_$(AM_DEFAULT_VERBOSITY))
am__v_PROTOC_C_0 = @echo "  PROTOC_C" $*.pb-c.c $*.pb-c.h;
am__v_PROTOC_C_1 =

%.pb-c.c %.pb-c.h: %.proto
	$(AM_V_PROTOC_C)$(PROTOC_C) --proto_path=$(srcdir) --c_out=$(builddir) $^
