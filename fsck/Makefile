ARCH=sun
PROG=fsck.$(ARCH)
OBJ=obj.$(ARCH)/
OBJS=$(OBJ)main.o $(OBJ)inode.o $(OBJ)pass1.o $(OBJ)pass1b.o $(OBJ)pass2.o $(OBJ)pass3.o $(OBJ)pass4.o $(OBJ)pass5.o $(OBJ)preen.o $(OBJ)setup.o $(OBJ)utilities.o $(OBJ)dir.o $(OBJ)ffs_subr.o $(OBJ)ffs_tables.o $(OBJ)other.o

CC=gcc
#CFLAGS=-O2 -I./include
CFLAGS=-I./include -Dcdh
RM=rm -f
DOTDOT=..

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJS)

clean:
	$(RM) $(OBJS) $(PROG)

$(OBJ)main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o $(OBJ)main.o

$(OBJ)inode.o: inode.c
	$(CC) $(CFLAGS) -c inode.c -o $(OBJ)inode.o

$(OBJ)pass1.o: pass1.c
	$(CC) $(CFLAGS) -c pass1.c -o $(OBJ)pass1.o

$(OBJ)pass1b.o: pass1b.c
	$(CC) $(CFLAGS) -c pass1b.c -o $(OBJ)pass1b.o

$(OBJ)pass2.o: pass2.c
	$(CC) $(CFLAGS) -c pass2.c -o $(OBJ)pass2.o

$(OBJ)pass3.o: pass3.c
	$(CC) $(CFLAGS) -c pass3.c -o $(OBJ)pass3.o

$(OBJ)pass4.o: pass4.c
	$(CC) $(CFLAGS) -c pass4.c -o $(OBJ)pass4.o

$(OBJ)pass5.o: pass5.c
	$(CC) $(CFLAGS) -c pass5.c -o $(OBJ)pass5.o

$(OBJ)preen.o: preen.c
	$(CC) $(CFLAGS) -c preen.c -o $(OBJ)preen.o

$(OBJ)setup.o: setup.c
	$(CC) $(CFLAGS) -c setup.c -o $(OBJ)setup.o

$(OBJ)utilities.o: utilities.c
	$(CC) $(CFLAGS) -c utilities.c -o $(OBJ)utilities.o

$(OBJ)dir.o: dir.c
	$(CC) $(CFLAGS) -c dir.c -o $(OBJ)dir.o

$(OBJ)other.o: other.c
	$(CC) $(CFLAGS) -c other.c -o $(OBJ)other.o

$(OBJ)ffs_subr.o: ../support/ffs_subr.c
	$(CC) $(CFLAGS) -c $(DOTDOT)/support/ffs_subr.c -o $(OBJ)ffs_subr.o

$(OBJ)ffs_tables.o: ../support/ffs_tables.c
	$(CC) $(CFLAGS) -c $(DOTDOT)/support/ffs_tables.c -o $(OBJ)ffs_tables.o
