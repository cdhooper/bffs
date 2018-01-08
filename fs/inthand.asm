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
;# Very simple interrupt handler
;#
;# This code was written by Ken Dyke at Commodore on 12/12/93
;# in support of a very confused person (me).  Thanks Ken!!!
;#
;# a1    - The is_Data field of the int structure is passed here
;# 4(a1) - The signal bits for the process
;#

	section text,code

	ds.l 0
	procstart
	XREF	_LVOSignal
	XDEF	_IntHandler

_IntHandler:
	move.l	a6,-(a7)	;# save a6
	move.l	a1,-(a7)	;# save a1
	movea.l	4,a6		;# Get execbase in a6
	move.l	4(a1),d0	;# get signal mask
	move.l	(a1),a1		;# get task pointer
	jsr	_LVOSignal(a6)	;# signal task
	move.l	(a7)+,a1	;# restore a1
	move.l	(a7)+,a6	;# restore a6
	clr.l	d0		;# make sure Z flag is set
	rts			;# bye
