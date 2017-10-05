ARCH=sun
PROG=dumpfs.$(ARCH)
OBJ=obj.$(ARCH)/
OBJS=$(OBJ)dumpfs.o $(OBJ)other.o

CC=gcc
CFLAGS=-I./include -Dcdh
RM=rm -f
DOTDOT=..

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJS)

clean:
	$(RM) $(OBJS) $(PROG)

$(OBJ)dumpfs.o: dumpfs.c
	$(CC) $(CFLAGS) -c dumpfs.c -o $(OBJ)dumpfs.o

$(OBJ)other.o: other.c
	$(CC) $(CFLAGS) -c other.c -o $(OBJ)other.o
