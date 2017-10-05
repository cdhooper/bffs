;# Very simple interrupt handler
;#
;# This code written by Ken Dyke at Commodore 12/12/93
;# in support of a very confused person (me).
;# Thanks Ken!!!
;#
;# a1    - The is_Data field of the int structure is passed here
;# 4(a1) - The signal bits for the process

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
