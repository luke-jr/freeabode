bin_PROGRAMS = remctl

if HAVE_JANSSON
bin_PROGRAMS += fabd-cli
fabd_cli_SOURCES = cli.c
fabd_cli_CFLAGS = $(JANSSON_CFLAGS) $(LIBZMQ_CFLAGS) $(PROTOBUF_C_CFLAGS)
fabd_cli_LDADD = ../libfreeabode/libfreeabode.la -lm $(JANSSON_LIBS) $(LIBZMQ_LIBS) $(PROTOBUF_C_LIBS)
endif

remctl_SOURCES = remctl.c
remctl_CFLAGS = $(LIBZMQ_CFLAGS) $(PROTOBUF_C_CFLAGS)
remctl_LDADD = ../libfreeabode/libfreeabode.la $(LIBZMQ_LIBS) $(PROTOBUF_C_LIBS)
