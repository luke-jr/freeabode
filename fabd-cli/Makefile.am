bin_PROGRAMS = remctl

if HAVE_JANSSON
bin_PROGRAMS += fabd-cli
fabd_cli_SOURCES = cli.c
fabd_cli_CFLAGS = $(JANSSON_CFLAGS)
fabd_cli_LDADD = ../libfreeabode/libfreeabode.la -lm $(JANSSON_LIBS)
endif

remctl_SOURCES = remctl.c
remctl_LDADD = ../libfreeabode/libfreeabode.la
