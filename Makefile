# Makefile for swiggle
#
# $Id: Makefile,v 1.8 2003/10/22 21:01:18 le Exp $
CC = gcc
PROGRAM = swiggle
OBJS = swiggle.o resize.o html.o
HEADER = swiggle.h
CFLAGS += -Wall -I/usr/local/include -I/usr/local/include/libexif
LDFLAGS += -L/usr/local/lib -ljpeg -lexif

all: ${PROGRAM}

${PROGRAM}: ${OBJS}
	gcc ${CFLAGS} ${LDFLAGS} -o ${PROGRAM} ${OBJS}

${OBJS}: ${HEADER}

install:
	install -c -o 0 -g 0 -s swiggle /usr/local/bin

clean:
	rm -f *.o *.core ${PROGRAM}
