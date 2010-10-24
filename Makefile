# A long time ago, far, far away, under Solaris, you needed to
#    CFLAGS += -xO2 -Xc
#    LDLIBS += -lnsl -lsocket
# To cross-compile
#    CC = arm-linux-gcc
# To check for lint
# -Wundef not recognized by gcc-2.7.2.3
CFLAGS += -std=c99 -W -Wall -Wpointer-arith -Wcast-align -Wcast-qual -Wshadow \
 -Waggregate-return -Wnested-externs -Winline -Wwrite-strings -Wstrict-prototypes

CFLAGS += -O2
# CFLAGS += -DPRECISION_SIOCGSTAMP
CFLAGS += -DENABLE_DEBUG
CFLAGS += -DENABLE_REPLAY
# CFLAGS += -DUSE_OBSOLETE_GETTIMEOFDAY

LDFLAGS += -lrt

all: ntpclient

test: ntpclient
	./ntpclient -d -r <test.dat

ntpclient: ntpclient.o phaselock.o

ntpclient.o phaselock.o: ntpclient.h

adjtimex: adjtimex.o

clean:
	rm -f ntpclient adjtimex *.o
