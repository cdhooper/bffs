#ifndef _UCB_SYS_MTIO_H
#define _UCB_SYS_MTIO_H

/* structure for MTIOCTOP - mag tape op command */
struct mtop {
	short	mt_op;		/* operations defined below */
	long	mt_count;	/* how many of them */
};

#define MTFSF		1	/* forward space file */
#endif /* _UCB_SYS_MTIO_H */
