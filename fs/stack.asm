;#
;# Copyright 2018 Chris Hooper <amiga@cdh.eebugs.com>
;#
;# Redistribution and use in source and binary forms, with or without
;# modification, are permitted so long as any redistribution retains the
;# above copyright notice, this condition, and the below disclaimer.
;#
;# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS"
;# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
;# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
;# ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
;# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
;# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
;# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
;# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
;# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
;# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
;# POSSIBILITY OF SUCH DAMAGE.
;#

;#
;# This routine is the _main routine of BFFS.  It is used to
;# set up sufficient stack to actually run the filesystem main
;# handler routine.
;#
;# This code was written by Ken Dyke at Commodore on 12/14/93
;# in support of a very confused person (me).  Thanks Ken!!!
;#

	section text,code
	ds.l 0
	procstart

	XDEF	__main		; Make this the main routine
	XREF	_handler   	; The "real" start of the FileSystem
	XREF	_LVOStackSwap	; exec's stack swap routine
	XREF	_LVOAllocVec	; exec's memory allocation routine
	XREF	_LVOFreeVec	; exec's memory deallocation routine

STACKSPACE	EQU	2048	; amount of stack to allocate

_LVOStackWap	EQU	-$2DC	; Taken from <pragmas/exec_pragmas.h>
stk_Lower	EQU	$0	; Taken from <exec/tasks.i>
stk_Upper	EQU	$4	; Taken from <exec/tasks.i>
stk_Pointer	EQU	$8	; Taken from <exec/tasks.i>


__main:
	movea.l	4,a6			; Grab ExecBase
	move.l	#STACKSPACE+12,d0	; Also Allocate StackSwap Structure
	clr.l	d1			; Any kind of memory will do
	suba.l	#12,sp			; Allocate space for a StackSwap struct
	jsr	_LVOAllocVec(a6)
	tst.l	d0
	beq	goexit			; Exit - no memory available

	move.l	d0,a2			; Save the allocated address
	move.l	d0,a3			; Save the allocated address
	add.l	#12,d0			; Bump past the StackSwap structure
	move.l	d0,stk_Lower(a2)	; Set Lower bound
	add.l	#STACKSPACE,d0		; Size+Lower bound
	move.l	d0,stk_Upper(a2)	; Set Upper bound
	sub.l	#4,d0			; Current stack pointer position
	move.l	d0,stk_Pointer(a2)	; Set current stack pointer

	movea.l	a2,a0			; Get StackSwap struct pointer
	jsr	_LVOStackSwap(a6)	; *POOF*

	jsr	_handler		; Do filesystem...

	movea.l	a2,a0			; Get StackSwap struct pointer
	jsr	_LVOStackSwap(a6)	; *UNPOOF*

	move.l	a3,a1			; Get allocated memory address
	jsr	_LVOFreeVec(a6)		; Free StackSwap and stack

goexit:	rts				; exit program, condition normal
