#CC=dcc
#CFLAGS=-ms -r
#ASM=das
#AFILES=
#LIBS=
#RM=delete force


CC=gcc -Wall
#CFLAGS=-nostdinc -nostdlib -resident gcc:lib/libnix/libnixmain.a gcc:lib/libnix/libnix.a gcc:lib/libnix/libstubs.a -Idcc:include
COPT=
#COPT=-O2 -fstrength-reduce
CFLAGS=$(COPT) -nostdinc -noixemul -fbaserel -resident -Idcc:include -Igcc:os-include -DGCC -D__USE_SYSBASE
#CFLAGS=-nostdinc -fbaserel -resident -Idcc:include -Igcc:os-include -DGCC -D__OPTIMIZE__
ASM=gcc -c
AFILES=g_
LIBS=-L GCC:lib/libb/libnix -lnux -lamiga -L GCC:os-lib/libb -lstubs
LD=ld -nostdlib
RM=rm -rf

PROG=gbffs

OD=obj/
OBJS=$(OD)stack.o $(OD)handler.o $(OD)debug.o $(OD)packets.o $(OD)ufs.o $(OD)system.o $(OD)fread.o $(OD)fwrite.o $(OD)file.o $(OD)inode.o $(OD)cache.o $(OD)dir.o $(OD)table.o $(OD)alloc.o $(OD)config.o $(OD)request.o $(OD)inthand.o

all:  gbffs dbprint

clean:
	$(RM) $(OBJS)

gbffs: $(OBJS)
	$(LD) $(OBJS) $(LIBS) -o $@

#	$(CC) $(CFLAGS) $(OBJS) $(LIBS) -o $@

dbprint: dbprint.c
	$(CC) $(CFLAGS) $< -o $@

$(OD)ufs.o: ufs.c config.h handler.h superblock.h ufs.h fsmacros.h file.h stat.h
	$(CC) $(CFLAGS) -c ufs.c -o $@

$(OD)handler.o: handler.c config.h handler.h packets.h table.h stat.h
	$(CC) $(CFLAGS) -c handler.c -o $@

$(OD)debug.o: debug.c config.h
	$(CC) $(CFLAGS) -c debug.c -o $@

$(OD)packets.o: packets.c config.h superblock.h fsmacros.h packets.h system.h handler.h file.h cache.h stat.h
	$(CC) $(CFLAGS) -c packets.c -o $@

$(OD)system.o: system.c config.h handler.h superblock.h ufs.h fsmacros.h packets.h system.h stat.h
	$(CC) $(CFLAGS) -c system.c -o $@

$(OD)fread.o: fread.c config.h ufs.h superblock.h fsmacros.h file.h handler.h
	$(CC) $(CFLAGS) -c fread.c -o $@

$(OD)fwrite.o: fwrite.c config.h fsmacros.h file.h alloc.h cache.h handler.h
	$(CC) $(CFLAGS) -c fwrite.c -o $@

$(OD)file.o: file.c config.h handler.h ufs.h file.h cache.h alloc.h
	$(CC) $(CFLAGS) -c file.c -o $@

$(OD)inode.o: inode.c config.h superblock.h file.h cache.h alloc.h
	$(CC) $(CFLAGS) -c inode.c -o $@

$(OD)cache.o: cache.c config.h superblock.h ufs.h fsmacros.h cache.h file.h handler.h stat.h
	$(CC) $(CFLAGS) -c cache.c -o $@

$(OD)dir.o: dir.c config.h ufs.h file.h handler.h cache.h
	$(CC) $(CFLAGS) -c dir.c -o $@

$(OD)table.o: table.c config.h table.h packets.h
	$(CC) $(CFLAGS) -c table.c -o $@

$(OD)request.o: request.c config.h request.h config.h
	$(CC) $(CFLAGS) -c request.c -o $@

$(OD)alloc.o: alloc.c config.h superblock.h alloc.h fsmacros.h cache.h
	$(CC) $(CFLAGS) -c alloc.c -o $@

$(OD)config.o: config.c alloc.h
	$(CC) $(CFLAGS) -c config.c -o $@

$(OD)inthand.o: $(AFILES)inthand.s
	$(ASM) $(AFILES)inthand.s -o $@

$(OD)stack.o: $(AFILES)stack.s
	$(ASM) $(AFILES)stack.s -o $@
