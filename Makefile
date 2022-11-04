TARGET=fat_fsck

CC = gcc
CPP = $(CC) -E
OPTFLAGS = -O2 -fomit-frame-pointer -D_FILE_OFFSET_BITS=64
WARNFLAGS = -Wall
CFLAGS = $(OPTFLAGS) $(WARNFLAGS)
LDFLAGS =

all : $(TARGET)

$(TARGET): fat_fsck.o
	$(CC) -o $@ $(LDFLAGS) $^

.c.o:
	$(CC) -c $(CFLAGS) $*.c

install: $(TARGET)
	mkdir -p $(SBINDIR)
	install -m 755 fat_fsck $(SBINDIR)

clean:
	rm -f *.o *.s *.i *~ \#*# tmp_make .#* .new*

distclean: clean
	rm -f *.a $(TARGET)

dep:
	sed '/\#\#\# Dependencies/q' <Makefile >tmp_make
	$(CPP) $(CFLAGS) -MM *.c >>tmp_make
	mv tmp_make Makefile

fat_fsck.o: fat_fsck.c version.h fat_fsck.h
