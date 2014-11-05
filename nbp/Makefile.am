bin_PROGRAMS = freeabode

freeabode_SOURCES = main.c nest.c nest.h crc.c crc.h
freeabode_CFLAGS = $(FREEABODE_CFLAGS) $(LIBZMQ_CFLAGS) $(PROTOBUF_C_CFLAGS)
freeabode_LDADD = $(FREEABODE_LIBS) $(LIBZMQ_LIBS) $(PROTOBUF_C_LIBS)
