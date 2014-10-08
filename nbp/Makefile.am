bin_PROGRAMS = freeabode

freeabode_SOURCES = main.c nest.c nest.h crc.c crc.h
freeabode_CFLAGS = $(LIBZMQ_CFLAGS) $(PROTOBUF_C_CFLAGS)
freeabode_LDADD = ../libfreeabode/libfreeabode.la $(LIBZMQ_LIBS) $(PROTOBUF_C_LIBS)
