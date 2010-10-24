# Under Solaris, you need to 
#    CFLAGS += -xO2 -Xc
#    LDLIBS += -lnsl -lsocket
# Some versions of Linux may need
#    CFLAGS += -D_GNU_SOURCE

CFLAGS = -Wall -O
TARFILE = $(shell date +"ntpclient_%Y_%j.tar.gz")

all: ntpclient

test: ntpclient
	./ntpclient -d -r <test.dat | less

ntpclient: ntpclient.o phaselock.o

tar: $(TARFILE)

$(TARFILE): README ntpclient.c phaselock.c Makefile envelope test.dat
	tar -cvzf $@ $^
		
clean:
	rm -f ntpclient *.o
