LDLIBS=-lm $(shell freetype-config --libs)
CFLAGS=-O2 $(shell freetype-config --cflags)

#CFLAGS+=-O0			# for RedHat's gcc-2.96-95
#CFLAGS+=-DOLD_FREETYPE2	# for FreeType 2.0.1
#CFLAGS+=-g
#CFLAGS+=-DDEBUG
#CFLAGS+=-DNEW_DESC


subfont:	subfont.o
	$(CC) $(CFLAGS) $< $(LDLIBS) -o $@

subfont.o:	subfont.c Makefile ../../mplayerxp/bswap.h

subfont.S:	subfont.c Makefile ../../mplayerxp/bswap.h
	$(CC) $(CFLAGS) -S $< -o $@

clean:
	rm -f subfont subfont.o core
