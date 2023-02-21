TARGET=wchkdsk

CC = gcc
CPP = $(CC) -E
OPTFLAGS = -O2 -fomit-frame-pointer -D_FILE_OFFSET_BITS=64
WARNFLAGS = -Wall
CFLAGS = $(OPTFLAGS) $(WARNFLAGS)
LDFLAGS =

all : $(TARGET)

$(TARGET): wchkdsk.o
	$(CC) -o $@ $(LDFLAGS) $^

.c.o:
	$(CC) -c $(CFLAGS) $*.c

install: $(TARGET)
	mkdir -p $(SBINDIR)
	install -m 755 wchkdsk $(SBINDIR)

clean:
	rm -f *.o *.s *.i *~ \#*# tmp_make .#* .new*

distclean: clean
	rm -f *.a $(TARGET)

dep:
	sed '/\#\#\# Dependencies/q' <Makefile >tmp_make
	$(CPP) $(CFLAGS) -MM *.c >>tmp_make
	mv tmp_make Makefile

wchkdsk.o: wchkdsk.c version.h wchkdsk.h
