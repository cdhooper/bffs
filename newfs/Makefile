ARCH=sun
DOTDOT=..
PROG=$(DOTDOT)/newfs.$(ARCH)
OBJ=$(DOTDOT)/obj.$(ARCH)/
OBJS=$(OBJ)newfs.o $(OBJ)mkfs.o $(OBJ)newfs_other.o $(OBJ)dkcksum.o $(OBJ)disklabel.o $(OBJ)file_label.o

CC=gcc
#CFLAGS=-O2 -I./include
CFLAGS=-I./include -Dcdh
RM=rm -f

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJS)

clean:
	$(RM) $(OBJS) $(PROG)

$(OBJ)newfs.o: newfs.c
	$(CC) $(CFLAGS) -c newfs.c -o $(OBJ)newfs.o

$(OBJ)mkfs.o: mkfs.c
	$(CC) $(CFLAGS) -c mkfs.c -o $(OBJ)mkfs.o

$(OBJ)newfs_other.o: newfs_other.c
	$(CC) $(CFLAGS) -c newfs_other.c -o $(OBJ)newfs_other.o

$(OBJ)dkcksum.o: $(DOTDOT)/disklabel/dkcksum.c
	$(CC) $(CFLAGS) -c $(DOTDOT)/disklabel/dkcksum.c -o $(OBJ)dkcksum.o

$(OBJ)disklabel.o: $(DOTDOT)/support/disklabel.c
	$(CC) $(CFLAGS) -c $(DOTDOT)/support/disklabel.c -o $(OBJ)disklabel.o

$(OBJ)file_label.o: $(DOTDOT)/support/file_label.c
	$(CC) $(CFLAGS) -c $(DOTDOT)/support/file_label.c -o $(OBJ)file_label.o
