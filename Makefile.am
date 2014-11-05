SUBDIRS = \
	. \
	libfreeabode \
	fabd-cli \
	htu21d \
	nbp \
	tstat \
	wallknob

all-local: freeabode

freeabode: libfreeabode
	$(LN_S) libfreeabode $@

dist-hook:
	cd $(distdir) && (test -L freeabode || $(LN_S) libfreeabode freeabode)
