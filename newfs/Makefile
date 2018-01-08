#
# Copyright 2018 Chris Hooper <amiga@cdh.eebugs.com>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted so long as any redistribution retains the
# above copyright notice, this condition, and the below disclaimer.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

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
