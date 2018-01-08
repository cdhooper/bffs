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

ARCH=x86
PROG=dump.$(ARCH)
OBJ=obj.$(ARCH)/
OBJS=$(OBJ)main.o $(OBJ)dumprmt.o $(OBJ)itime.o $(OBJ)optr.o $(OBJ)traverse.o $(OBJ)tape.o $(OBJ)unctime.o $(OBJ)utilities.o
CC=gcc
#CFLAGS=-g -I./include -DRDUMP
#CFLAGS=-g -I./include -DUNIX_PORT -Dcdh -DDEBUG -DTDEBUG -DFDEBUG -DWRITEDEBUG
#CFLAGS=-O2 -I./include -DUNIX_PORT -Dcdh
CFLAGS=-g -I../ucbinclude -I../restore -DUNIX_PORT -Dcdh
RM=rm -f

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJS)

clean:
	$(RM) $(OBJS) $(PROG)

$(OBJ)main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o $(OBJ)main.o

$(OBJ)dumprmt.o: dumprmt.c
	$(CC) $(CFLAGS) -c dumprmt.c -o $(OBJ)dumprmt.o

$(OBJ)itime.o: itime.c
	$(CC) $(CFLAGS) -c itime.c -o $(OBJ)itime.o

$(OBJ)optr.o: optr.c
	$(CC) $(CFLAGS) -c optr.c -o $(OBJ)optr.o

$(OBJ)traverse.o: traverse.c
	$(CC) $(CFLAGS) -c traverse.c -o $(OBJ)traverse.o

$(OBJ)tape.o: tape.c
	$(CC) $(CFLAGS) -c tape.c -o $(OBJ)tape.o

$(OBJ)unctime.o: unctime.c
	$(CC) $(CFLAGS) -c unctime.c -o $(OBJ)unctime.o

$(OBJ)utilities.o: utilities.c
	$(CC) $(CFLAGS) -c utilities.c -o $(OBJ)utilities.o
