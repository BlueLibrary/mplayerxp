include ../../mpxp_config.mak

CXXSRCS=rtsp.cpp rtsp_rtp.cpp rtsp_session.cpp
SRCS = 
OBJS=$(SRCS:.c=.o)
CXXOBJS=$(CXXSRCS:.cpp=.o)

CFLAGS  = $(OPTFLAGS) -I. -I.. -I../.. $(EXTRA_INC) -Wall
CXXFLAGS  = $(OPTXXFLAGS) -I. -I.. -I../.. $(EXTRA_INC) -Wall

.SUFFIXES: .c .o

all:    dep $(OBJS) $(CXXOBJS)

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<
.cpp.o:
	$(CXX) -c $(CXXFLAGS) -o $@ $<

clean:
	rm -f *.o *.a *~

distclean:
	rm -f Makefile.bak *.o *.a *~ .depend

dep:    .depend

.depend: $(SRCS) $(CXXSRCS)
	$(CXX) -MM $(CXXFLAGS) $(SRCS) $(CXXSRCS) 1>.depend

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif

