ARCH=sun
PROG=tunefs.$(ARCH)
OBJ=obj.$(ARCH)/
OBJS=$(OBJ)tunefs.o $(OBJ)other.o

CC=gcc
#CFLAGS=-O2 -I./include
CFLAGS=-I./include -Dcdh
RM=rm -f
DOTDOT=..

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJS)

clean:
	$(RM) $(OBJS) $(PROG)

$(OBJ)tunefs.o: tunefs.c
	$(CC) $(CFLAGS) -c tunefs.c -o $(OBJ)tunefs.o

$(OBJ)other.o: other.c
	$(CC) $(CFLAGS) -c other.c -o $(OBJ)other.o
