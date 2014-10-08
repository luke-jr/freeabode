bin_PROGRAMS = htu21d

htu21d_SOURCES = htu21d.c
htu21d_CFLAGS = $(LIBZMQ_CFLAGS) $(PROTOBUF_C_CFLAGS)
htu21d_LDADD = ../libfreeabode/libfreeabode.la $(LIBZMQ_LIBS) $(PROTOBUF_C_LIBS)
