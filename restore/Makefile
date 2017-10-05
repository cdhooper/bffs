ARCH=sun
PROG=restore.$(ARCH)
OBJ=obj.$(ARCH)/
OBJS=$(OBJ)main.o $(OBJ)interactive.o $(OBJ)restore.o $(OBJ)dirs.o $(OBJ)symtab.o $(OBJ)tape.o $(OBJ)utilities.o $(OBJ)strsep.o $(OBJ)glob.o $(OBJ)other.o

CC=gcc
CFLAGS=-I../ucbinclude -Dcdh
CFLAGS += -O2
#CFLAGS += -static
#CFLAGS += -DRRESTORE
#OBJS += $(OBJ)dumprmt.o
RM=rm -f

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJS)

clean:
	$(RM) $(OBJS) $(PROG)

$(OBJ)main.o: main.c restore.h
	$(CC) $(CFLAGS) -c main.c -o $(OBJ)main.o

$(OBJ)interactive.o: interactive.c restore.h
	$(CC) $(CFLAGS) -c interactive.c -o $(OBJ)interactive.o

$(OBJ)restore.o: restore.c restore.h
	$(CC) $(CFLAGS) -c restore.c -o $(OBJ)restore.o

$(OBJ)dirs.o: dirs.c restore.h
	$(CC) $(CFLAGS) -c dirs.c -o $(OBJ)dirs.o

$(OBJ)symtab.o: symtab.c restore.h
	$(CC) $(CFLAGS) -c symtab.c -o $(OBJ)symtab.o

$(OBJ)tape.o: tape.c restore.h
	$(CC) $(CFLAGS) -c tape.c -o $(OBJ)tape.o

$(OBJ)utilities.o: utilities.c restore.h
	$(CC) $(CFLAGS) -c utilities.c -o $(OBJ)utilities.o

$(OBJ)glob.o: glob.c
	$(CC) $(CFLAGS) -c glob.c -o $(OBJ)glob.o

$(OBJ)strsep.o: strsep.c
	$(CC) $(CFLAGS) -c strsep.c -o $(OBJ)strsep.o

$(OBJ)dumprmt.o: dumprmt.c
	$(CC) $(CFLAGS) -c dumprmt.c -o $(OBJ)dumprmt.o

$(OBJ)other.o: other.c
	$(CC) $(CFLAGS) -c other.c -o $(OBJ)other.o
