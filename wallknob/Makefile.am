if HAVE_DIRECTFB

bin_PROGRAMS = wallknob

wallknob_SOURCES = \
	textmenu.c \
	wallknob.c \
	wallknob.h
wallknob_CFLAGS = $(DIRECTFB_CFLAGS) $(LIBZMQ_CFLAGS) $(PROTOBUF_C_CFLAGS)
wallknob_LDADD = ../libfreeabode/libfreeabode.la $(DIRECTFB_LIBS) -lm $(LIBZMQ_LIBS) $(PROTOBUF_C_LIBS)

endif
