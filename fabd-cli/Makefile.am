bin_PROGRAMS = remctl

if HAVE_JANSSON
bin_PROGRAMS += fabd-cli
fabd_cli_SOURCES = cli.c
fabd_cli_CFLAGS = $(FREEABODE_CFLAGS) $(JANSSON_CFLAGS) $(LIBZMQ_CFLAGS) $(PROTOBUF_C_CFLAGS)
fabd_cli_LDADD = $(FREEABODE_LIBS) -lm $(JANSSON_LIBS) $(LIBZMQ_LIBS) $(PROTOBUF_C_LIBS)
endif

remctl_SOURCES = remctl.c
remctl_CFLAGS = $(FREEABODE_CFLAGS) $(LIBZMQ_CFLAGS) $(PROTOBUF_C_CFLAGS)
remctl_LDADD = $(FREEABODE_LIBS) $(LIBZMQ_LIBS) $(PROTOBUF_C_LIBS)
