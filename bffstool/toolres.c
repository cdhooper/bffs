/* toolres.c version 1.5  (BFFStool)
 *      This program is copyright (1993 - 1996) Chris Hooper.  All code
 *      herein is freeware.  No portion of this code may be sold for profit.
 */

#include <stdio.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>


struct NewWindow window_ops =
{
	0, 0, 640, 184,
	-1, -1,
	MXIDCMP | CLOSEWINDOW | REFRESHWINDOW | GADGETUP,
	ACTIVATE | WINDOWDRAG | WINDOWDEPTH | WINDOWCLOSE | SIMPLE_REFRESH,
	NULL,
	NULL,
	"Berkeley Fast Filesystem (BFFS) status tool V1.5",
	NULL,
	NULL,
	0, 0, 0, 0,
	WBENCHSCREEN
};


struct TextAttr	text_font =
{
	"topaz.font",
	8,
	0, 0
};


char *fieldnames[] =
{
/* identifiers  (0 1 2)							*/
	"magic", "size", "unused",
/* tables	(3 4 5)							*/
	"superblock", "cache_head", "cache_hash",
/* cache	(6 7 8 9)						*/
	"max buffers", "cg max buffers", "dirty buffers", "total buffers",
/* special	(10 11 12, 13 14 15, 16)				*/
	"media start", "media end  ", "in use buffers",
	"automatic symlinks", "cache independent", "comments",
	"comments2", "cache used", "sync timer", "sync stalls",
	"GMT offset", "minfree", "og perm invert",
/* next starts at 23, not 17! */
/* cache stats	(17 18 19 20, 21 22 23 24, 25 26 27 28, 29 30 31 32)	*/
	"read hits  ", "write hits  ", "read misses", "write misses",
	"cgread hits  ", "cgwrite hits  ", "cgread misses", "cgwrite misses",
	"invalidates", "locked buffers", "cache moves", "flushes",
	"destroys", "force writes", "cg flushes", "cg force writes",
/* OS		(33 34 35 36, 37 38 39 40, 41 42 43 44 45) */
	"read groups", "write groups", "read bytes ", "write bytes ",
	"locates", "examines", "examinenexts", "flushes",
	"read opens ", "write opens ", "renames", "Version", "Disk Type",
	"PreAlloc", "read Kbytes", "write Kbytes",
/* superblock starts at 53  (dyn and static)	*/
        "num inodes",
	"inodes free", "blocks free", "frags free", "num dirs",
	"block size", "frag size", "data frags", "num frags",
	"num cgs", "num cyl",
	"sec/track", "track/cyl", "cyl/cg", "frags/cg", "inodes/cg",
	"minfree", "modified", "clean",
	NULL
};
