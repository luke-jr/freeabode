if HAVE_DIRECTFB

bin_PROGRAMS = wallknob

wallknob_SOURCES = \
	textmenu.c \
	wallknob.c \
	wallknob.h
wallknob_CFLAGS = $(FREEABODE_CFLAGS) $(DIRECTFB_CFLAGS) $(LIBZMQ_CFLAGS) $(PROTOBUF_C_CFLAGS)
wallknob_LDADD = $(FREEABODE_LIBS) $(DIRECTFB_LIBS) -lm $(LIBZMQ_LIBS) $(PROTOBUF_C_LIBS)

endif
