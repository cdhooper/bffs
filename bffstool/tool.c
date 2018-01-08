/* tool.c version (BFFStool)
 *      This program is copyright (1993 - 2018) Chris Hooper.  All code
 *      herein is freeware.  No portion of this code may be sold for profit.
 */

const char *version = "\0$VER: bffstool 1.6 (19-Jan-2018) © Chris Hooper";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <intuition/gadgetclass.h>
#include <time.h>
#include <superblock.h>
#include <ufs/fs.h>
#include "tool.h"
#include "comm.h"

#include <clib/exec_protos.h>
#include <clib/intuition_protos.h>
#include <clib/gadtools_protos.h>

#define ID_GENERIC   1
#define ID_REFRESH   2
#define ID_CLEAR     3
#define ID_HANDLER   4
#define ID_SYNC      5
#define ID_PATH      6
#define ID_RESOLVE   7
#define ID_RONLY     8
#define ID_CASE      9
#define ID_COMMENT  10
#define ID_COMMENT2 11
#define ID_MINFREE  12
#define ID_OGINVERT 13
#define ID_UDGAD    20
#define ID_QUIT    250

#define MAX_GADGETS    100
#define MAX_BFFSs	50
#define MAX_HASHITEMS	40
#define CACHE_CG_MAX	16

struct Screen *screen	= NULL;
struct Window *window	= NULL;
struct Gadget *gadlist	= NULL;
void	      *visual	= NULL;
struct stat   *stat	= NULL;
struct Gadget *gadgets[MAX_GADGETS];
struct Gadget *hagad	= NULL;
struct Gadget *upgad	= NULL;
struct Gadget *asgad	= NULL;
struct Gadget *rogad	= NULL;
struct Gadget *cigad	= NULL;
struct Gadget *dcgad	= NULL;
struct Gadget *dc2gad	= NULL;
struct Gadget *vgad	= NULL;
struct Gadget *dtgad	= NULL;
struct Gadget *mfgad	= NULL;
struct Gadget *pigad	= NULL;
struct Gadget *smgad	= NULL;
struct Gadget *scgad	= NULL;
struct Gadget *gad	= NULL;
struct Gadget *preallocgad = NULL;
struct NewGadget ng;
ULONG	      *stats	= NULL;
int	      sb_mod = 0;
int	      sb_clean = 0;
int	      prealloc = 0;
int           rkbytes = 0;
int           wkbytes = 0;
int	      stat_start 	= 23;
int           prealloc_pos	= 52;
int           rkbytes_pos	= 53;
int           wkbytes_pos	= 54;
int           super_max_inodes	= 55;
int	      super_start	= 56;
/* int	      hashlist_start; */
int	      total_gadgets = 0;
int	      *gad_addresses[MAX_GADGETS];
struct fs     *superblock;
char	      handler_name[128];
struct cache_set *temp;
int	      zero = 0;
char	      *filesystems[MAX_BFFSs] = { NULL };
char	      *handler_label[2] = { NULL, NULL };
ULONG         max_inodes = 0;
int	      ypos = 0;
int	      xpos = 0;
char	      prealloc_str[32];

static void calc_prealloc(void);
static void refresh_prealloc(void);

static void
cleanup(void)
{
	if (window)
		CloseWindow(window);
	if (gadlist)
		FreeGadgets(gadlist);
	if (visual)
		FreeVisualInfo(visual);
	if (screen)
		UnlockPubScreen(NULL, screen);
	if (fs)
		close_handler();
}

static void
errorexit(char *msg)
{
	if (msg)
		fprintf(stderr, msg);
	cleanup();
	exit(1);
}

static void
do_ng(WORD left, WORD top, WORD width, WORD height, UBYTE *text, UWORD id,
      ULONG flags)
{
	ng.ng_LeftEdge   = left;
        ng.ng_TopEdge	 = top;
        ng.ng_Width	 = width;
        ng.ng_Height	 = height;
        ng.ng_GadgetText = text;
        ng.ng_GadgetID   = id;
        ng.ng_Flags	 = flags;

        ng.ng_TextAttr	 = &text_font;
        ng.ng_VisualInfo = visual;
}

static void
assign_superblock_addresses(void)
{
    int current = super_start;

    gad_addresses[current++] = &superblock->fs_cstotal.cs_nifree; /* inodes free */
    gad_addresses[current++] = &superblock->fs_cstotal.cs_nbfree; /* blocks free */
    gad_addresses[current++] = &superblock->fs_cstotal.cs_nffree; /* frags free */
    gad_addresses[current++] = &superblock->fs_cstotal.cs_ndir; /* num dirs */
    gad_addresses[current++] = &superblock->fs_bsize;       /* block size */
    gad_addresses[current++] = &superblock->fs_fsize;       /* frag size */
    gad_addresses[current++] = &superblock->fs_dsize;       /* data frags */
    gad_addresses[current++] = &superblock->fs_size;        /* num frags */
    gad_addresses[current++] = &superblock->fs_ncg;         /* num cgs */
    gad_addresses[current++] = &superblock->fs_ncyl;        /* num cyl */
    gad_addresses[current++] = &superblock->fs_nsect;       /* sec/track */
    gad_addresses[current++] = &superblock->fs_ntrak;       /* track/cyl */
    gad_addresses[current++] = &superblock->fs_cpg;         /* cyl/cg */
    gad_addresses[current++] = &superblock->fs_fpg;         /* frags/cg */
    gad_addresses[current++] = &superblock->fs_ipg;         /* inodes/cg */
    gad_addresses[current++] = &superblock->fs_minfree;         /* minfree */
    gad_addresses[current++] = &sb_mod;             /* modified */
    gad_addresses[current++] = &sb_clean;               /* clean */
/*  gad_addresses[current++] = &superblock->fs_fmod;         * modified */
/*  gad_addresses[current++] = &superblock->fs_clean;        * clean */
}

static void
vertical_int(int num)
{
	do_ng(xpos, ypos, strlen(fieldnames[num]) * 8, 12,
	      fieldnames[num], 1, NG_HIGHLABEL | PLACETEXT_LEFT);
	gad = gadgets[num] = CreateGadget(NUMBER_KIND, gad, &ng, GTNM_Number,
					  *gad_addresses[num], TAG_DONE);
	ypos += 8;
}

static void
horizontal_int(int num, int *addr)
{
	do_ng(xpos, ypos, 0, 12, "", 1, NG_HIGHLABEL | PLACETEXT_LEFT);
	gad = gadgets[num] = CreateGadget(NUMBER_KIND, gad, &ng,
					  GTNM_Number, *addr,
					  GTTX_Border, 1, TAG_DONE);
	if (num > MAX_GADGETS)
		printf("Too many gadgets %d\n", num);
	else
		gad_addresses[num] = addr;
	xpos += 7 * 8;
}

static void
assign_stat_addresses(void)
{
	int index;

	gad_addresses[6]  = stat->cache_size;		/* max buffers */
	gad_addresses[7]  = stat->cache_cg_size;	/* cg max buffers */
	gad_addresses[8]  = stat->cache_item_dirty;	/* dirty buffers */
	gad_addresses[9]  = stat->cache_alloced;	/* alloced buffers */
	gad_addresses[10] = stat->disk_poffset;		/* media start */
	gad_addresses[11] = stat->disk_pmax;		/* media end */
	gad_addresses[12] = stat->cache_used;		/* cache in use */
	gad_addresses[18] = stat->timer_secs;		/* sync timer */
	gad_addresses[19] = stat->timer_loops;		/* sync stalls */
	gad_addresses[20] = stat->GMT;			/* GMT offset */

	for (index = stat_start; index < prealloc_pos; index++)
		gad_addresses[index] = &stats[index];
}

static void
setup_gadgets(void)
{
	int	index;
	int	current;

	for (current = 0; current < MAX_GADGETS; current++) {
		gad_addresses[current] = NULL;
		gadgets[current] = NULL;
	}

	gad = CreateContext(&gadlist);
	if (gad == NULL)
		errorexit("Unable to create a gadget context\n");

	ypos = 12;

	do_ng(554, ypos, 80, 14, "Refresh", ID_REFRESH,
	      NG_HIGHLABEL | PLACETEXT_IN);
	gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);
	ypos += 15;

	do_ng(554, ypos, 80, 14, "Sync", ID_SYNC,
	      NG_HIGHLABEL | PLACETEXT_IN);
	gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);
	ypos += 15;

	do_ng(554, ypos, 80, 14, "Clear", ID_CLEAR,
	      NG_HIGHLABEL | PLACETEXT_IN);
	gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);
	ypos += 15;

	do_ng(554, ypos, 80, 14, "Quit", ID_QUIT,
	      NG_HIGHLABEL | PLACETEXT_IN);
	gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);
	ypos += 15;

	do_ng(608, ypos, 20, 8, "Auto Symlink", ID_RESOLVE,
	      NG_HIGHLABEL | PLACETEXT_LEFT);
	asgad = gad = CreateGadget(CHECKBOX_KIND, gad, &ng, GTCB_Checked,
			   *stat->resolve_symlinks, TAG_DONE);
	ypos += 12;

	do_ng(608, ypos, 20, 8, "Ignore Case", ID_CASE,
	      NG_HIGHLABEL | PLACETEXT_LEFT);
	cigad = gad = CreateGadget(CHECKBOX_KIND, gad, &ng, GTCB_Checked,
			   *stat->case_independent, TAG_DONE);
	ypos += 12;

	do_ng(608, ypos, 20, 8, "Link Comments", ID_COMMENT,
	      NG_HIGHLABEL | PLACETEXT_LEFT);
	dcgad = gad = CreateGadget(CHECKBOX_KIND, gad, &ng, GTCB_Checked,
			   *stat->link_comments, TAG_DONE);
	ypos += 12;

	do_ng(608, ypos, 10, 8, "Inode Comments", ID_COMMENT2,
	      NG_HIGHLABEL | PLACETEXT_LEFT);
	dc2gad = gad = CreateGadget(CHECKBOX_KIND, gad, &ng, GTCB_Checked,
			   *stat->inode_comments, TAG_DONE);
	ypos += 12;

	do_ng(608, ypos, 20, 8, "Unix Paths", ID_PATH,
	      NG_HIGHLABEL | PLACETEXT_LEFT);
	upgad = gad = CreateGadget(CHECKBOX_KIND, gad, &ng, GTCB_Checked,
			   *stat->unix_paths, TAG_DONE);
	ypos += 12;

	do_ng(608, ypos, 20, 8, "Read Only", ID_RONLY,
	      NG_HIGHLABEL | PLACETEXT_LEFT);
	rogad = gad = CreateGadget(CHECKBOX_KIND, gad, &ng, GTCB_Checked,
			   superblock->fs_ronly, TAG_DONE);
	ypos += 12;

	do_ng(608, ypos, 20, 8, "Show Minfree", ID_MINFREE,
	      NG_HIGHLABEL | PLACETEXT_LEFT);
	mfgad = gad = CreateGadget(CHECKBOX_KIND, gad, &ng, GTCB_Checked,
			   *stat->minfree, TAG_DONE);
	ypos += 12;

	do_ng(608, ypos, 20, 8, "Grp/Otr Bits", ID_OGINVERT,
	      NG_HIGHLABEL | PLACETEXT_LEFT);
	pigad = gad = CreateGadget(CHECKBOX_KIND, gad, &ng, GTCB_Checked,
			   *stat->og_perm_invert, TAG_DONE);


	do_ng(128, 132, 5, 4, "", ID_UDGAD, 0);
	gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);
	do_ng(127, 136, 5, 4, "", ID_UDGAD + 1, 0);
	gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);

	do_ng(128, 141, 5, 4, "", ID_UDGAD + 2, 0);
	gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);
	do_ng(127, 145, 5, 4, "", ID_UDGAD + 3, 0);
	gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);

	do_ng(288, 104, 5, 4, "", ID_UDGAD + 4, 0);
	gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);
	do_ng(287, 108, 5, 4, "", ID_UDGAD + 5, 0);
	gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);

	do_ng(288, 114, 5, 4, "", ID_UDGAD + 6, 0);
	gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);
	do_ng(287, 118, 5, 4, "", ID_UDGAD + 7, 0);
	gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);

	do_ng(288, 124, 5, 4, "", ID_UDGAD + 8, 0);
	gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);
	do_ng(287, 128, 5, 4, "", ID_UDGAD + 9, 0);
	gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);

	ypos = window->BorderTop;
	xpos = window->BorderLeft + 130;

	assign_stat_addresses();
	assign_superblock_addresses();

	vertical_int(stat_start);	/* read hits */
	vertical_int(stat_start +  2);	/* read misses */
	vertical_int(stat_start +  1);	/* write hits */
	vertical_int(stat_start +  3);	/* write misses */

	vertical_int(stat_start +  4);	/* cgread hits */
	vertical_int(stat_start +  6);	/* cgread misses */
	vertical_int(stat_start +  5);	/* cgwrite hits */
	vertical_int(stat_start +  7);	/* cgwrite misses */
	vertical_int(stat_start + 10);	/* cache moves */
	vertical_int(stat_start + 11);	/* flushes */
	vertical_int(stat_start + 14);	/* cg flushes */
	vertical_int(stat_start + 13);	/* force writes */
	vertical_int(stat_start + 15);	/* cg force writes */
	vertical_int(stat_start + 12);	/* destroys */
	vertical_int(stat_start +  8);	/* invalidates */

	vertical_int(7);		/* cg max buffers */
	vertical_int(6);		/* max buffers */
	vertical_int(9);		/* alloced buffers */
	vertical_int(12);		/* in use buffers */
	vertical_int(8);		/* dirty buffers */
	vertical_int(stat_start +  9);	/* locked buffers */


	xpos += 160;
	ypos = window->BorderTop;

	vertical_int(stat_start + 24);	/* read opens */
	vertical_int(stat_start + 16);	/* read groups */

	rkbytes = (stat->direct_read_bytes) / 1024;
	gad_addresses[rkbytes_pos] = &rkbytes;    /* rkbytes calculated */
	vertical_int(rkbytes_pos);
/*	vertical_int(stat_start + 18);	 * read bytes */

	vertical_int(stat_start + 25);	/* write opens */
	vertical_int(stat_start + 17);	/* write groups */

	wkbytes = (stat->direct_write_bytes) / 1024;
	gad_addresses[wkbytes_pos] = &wkbytes;    /* wkbytes calculated */
	vertical_int(wkbytes_pos);
/*	vertical_int(stat_start + 19);	 * write bytes */

	vertical_int(stat_start + 20);	/* locates */
	vertical_int(stat_start + 21);	/* examines */
	vertical_int(stat_start + 22);	/* examinenexts */
	vertical_int(stat_start + 23);	/* flushes */
	vertical_int(stat_start + 26);	/* renames */

	ypos += 4;

	vertical_int(18);		/* sync timer */
	ypos += 2;
	vertical_int(19);		/* sync stalls */
	ypos += 2;

	vertical_int(20);		/* GMT offset */
	ypos += 4;

	calc_prealloc();
	do_ng(xpos, ypos, strlen(fieldnames[prealloc_pos]) * 8, 12,
	      fieldnames[prealloc_pos], 1, NG_HIGHLABEL | PLACETEXT_LEFT);
	preallocgad = gad = CreateGadget(TEXT_KIND, gad, &ng, GTTX_Text,
				         prealloc_str, TAG_DONE);
	ypos += 12;

	vertical_int(10);		/* media start */
	vertical_int(11);		/* media end */

	do_ng(xpos, ypos, 60, 12, "version", 1,
	      NG_HIGHLABEL | PLACETEXT_LEFT);
	vgad = gad = CreateGadget(TEXT_KIND, gad, &ng, GTTX_Text,
				  stat->handler_version, TAG_DONE);
	ypos += 8;

	do_ng(xpos, ypos, 60, 12, "disk type", 1,
	      NG_HIGHLABEL | PLACETEXT_LEFT);
	dtgad = gad = CreateGadget(TEXT_KIND, gad, &ng, GTTX_Text,
				   stat->disk_type, TAG_DONE);
	ypos += 8;


	ypos = window->BorderTop;
	xpos += 160;

	max_inodes = superblock->fs_ipg * superblock->fs_ncg;
	gad_addresses[super_max_inodes] = &max_inodes;	    /* num inodes */

	current = super_start;
	for (index = 0; index < 6; index++, current++)
		vertical_int(current);
	vertical_int(super_max_inodes);
	for (; gad_addresses[current] != NULL; current++)
		vertical_int(current);


/* below displays detailed cache LRU information
	hashlist_start = current;
	xpos = 182;
	ypos += 8;
	do_ng(xpos - 8, ypos, 0, 9, "Cache LRU block list", 0,
	      NG_HIGHLABEL | PLACETEXT_RIGHT);
	gad = CreateGadget(TEXT_KIND, gad, &ng, TAG_DONE);
	ypos += 8;

	for (temp = cache_stack; temp != NULL; temp = temp->stack_up) {
		horizontal_int(current++, &temp->blk);
		if (xpos > 620) {
			xpos = 182;
			ypos += 8;
		}
		if (current >= hashlist_start + MAX_HASHITEMS)
			break;
	}
	while (current < hashlist_start + MAX_HASHITEMS) {
		horizontal_int(current++, &zero);
		if (xpos > 620) {
			xpos = 182;
			ypos += 8;
		}
	}
*/

	total_gadgets = current;
	if (filesystems[0] == NULL)
		errorexit("There are no BBFS filesystem handlers running\n");

	do_ng(454, 168, 181, 14, "", ID_HANDLER, 0);

	if (!strchr(handler_name, ':'))
		strcat(handler_name, ":");

	handler_label[0] = handler_name;
	handler_label[1] = NULL;
	hagad = gad = CreateGadget(CYCLE_KIND, gad, &ng, GTCY_Labels, handler_label,
				   GTCY_Active, 0, TAG_DONE);

	if (!gad)
		errorexit("Couldn't allocate the gadget list!\n");

	AddGList(window, gadlist, (UWORD) -1, (UWORD) -1, NULL);
	RefreshGList(gadlist, window, NULL, (UWORD) -1);
	GT_RefreshWindow(window, NULL);
}

static void
refresh_information(void)
{
	int index;
	struct stat	*tstat;
	struct MsgPort	*ofs;
	struct fs	*osb;

	ofs = fs;
	osb = superblock;

	if (open_handler(handler_name))
		printf("Error, %s has been unmounted\n", handler_name);
	else {
	    tstat = get_stat();
	    if (!tstat)
		printf("Error getting status information, %s is not responding.\n",
			handler_name);
	    else {
		stat  = tstat;
		stats = (ULONG *) stat;

		assign_stat_addresses();
		assign_superblock_addresses();
	    }
	}

/* not printing hash any more
	index = hashlist_start;
	for (temp = cache_stack; ((temp != NULL) && (index < total_gadgets));
	     temp = temp->stack_up, index++)
		gad_addresses[index] = &temp->blk;

	for (; index < total_gadgets; index++)
		gad_addresses[index] = &zero;
*/
	GT_SetGadgetAttrs(asgad, window, NULL, GTCB_Checked,
			  *stat->resolve_symlinks, TAG_DONE);

	GT_SetGadgetAttrs(cigad, window, NULL, GTCB_Checked,
			  *stat->case_independent, TAG_DONE);

	GT_SetGadgetAttrs(dcgad, window, NULL, GTCB_Checked,
			  *stat->link_comments, TAG_DONE);

	GT_SetGadgetAttrs(dc2gad, window, NULL, GTCB_Checked,
			  *stat->inode_comments, TAG_DONE);

	GT_SetGadgetAttrs(upgad, window, NULL, GTCB_Checked,
			  *stat->unix_paths, TAG_DONE);

	GT_SetGadgetAttrs(rogad, window, NULL, GTCB_Checked,
			  superblock->fs_ronly, TAG_DONE);

	GT_SetGadgetAttrs(mfgad, window, NULL, GTCB_Checked,
			  *stat->minfree, TAG_DONE);

	GT_SetGadgetAttrs(pigad, window, NULL, GTCB_Checked,
			  *stat->og_perm_invert, TAG_DONE);

	GT_SetGadgetAttrs(vgad, window, NULL, GTTX_Text,
			  stat->handler_version, TAG_DONE);

	GT_SetGadgetAttrs(dtgad, window, NULL, GTTX_Text,
			  stat->disk_type, TAG_DONE);

	sb_mod   = ((unsigned char) superblock->fs_fmod);
	sb_clean = ((unsigned char) superblock->fs_clean);

	prealloc = (*stat->resolve_symlinks ?  0 : 1 ) |
		   (*stat->case_independent ?  0 : 2 ) |
		   (*stat->unix_paths       ?  0 : 4 ) |
		   (*stat->link_comments    ?  8 : 0 ) |
		   (*stat->inode_comments   ? 16 : 0 ) |
		   (*stat->og_perm_invert   ? 32 : 0 ) |
		   (*stat->minfree          ? 64 : 0 ) |
		   (((unsigned char) *stat->GMT) << 8);

	rkbytes = (stat->direct_read_bytes)  / 1024;
	wkbytes = (stat->direct_write_bytes) / 1024;

	max_inodes = superblock->fs_ipg * superblock->fs_ncg;

	for (index = 1; index < total_gadgets; index++)
	    if (gadgets[index] && gad_addresses[index])
		GT_SetGadgetAttrs(gadgets[index], window, NULL, GTNM_Number,
				  *gad_addresses[index], TAG_DONE);

	GT_RefreshWindow(window, NULL);
}

static void
refresh_handlers(void)
{
	handler_label[0] = handler_name;

	GT_SetGadgetAttrs(hagad, window, NULL, GTCY_Labels,
			  handler_label, GTCY_Active, 0, TAG_DONE);
	GT_RefreshWindow(window, NULL);
}

static void
refresh_udgad_info(void)
{
	int index;

	if (gadgets[index = 6])
	    GT_SetGadgetAttrs(gadgets[index], window, NULL, GTNM_Number,
			      *gad_addresses[index], TAG_DONE);
	if (gadgets[index = 7])
	    GT_SetGadgetAttrs(gadgets[index], window, NULL, GTNM_Number,
			      *gad_addresses[index], TAG_DONE);
	if (gadgets[index = 18])
	    GT_SetGadgetAttrs(gadgets[index], window, NULL, GTNM_Number,
			      *gad_addresses[index], TAG_DONE);
	if (gadgets[index = 19])
	    GT_SetGadgetAttrs(gadgets[index], window, NULL, GTNM_Number,
			      *gad_addresses[index], TAG_DONE);
	if (gadgets[index = 20])
	    GT_SetGadgetAttrs(gadgets[index], window, NULL, GTNM_Number,
			      *gad_addresses[index], TAG_DONE);

	GT_RefreshWindow(window, NULL);
}

static void
clear_stats(void)
{
	int index;
	int last = (sizeof(struct stat) -
		    sizeof(stat->handler_version) -
		    sizeof(stat->disk_type)) /
		   sizeof(ULONG);
	for (index = stat_start; index < last; index++)
		*(stats + index) = 0;
}

static void
handle_gadget(struct Gadget *gadget, UWORD code)
{
	static int lasthandler = 0;
	int handler;

	switch (gadget->GadgetID) {
	    case ID_QUIT:
		errorexit(NULL);
	    case ID_GENERIC:
		fprintf(stderr, "generic id?\n");
		break;
	    case ID_REFRESH:
		refresh_information();
		break;
	    case ID_SYNC:
		sync_filesystem();
		refresh_information();
		break;
	    case ID_CLEAR:
		clear_stats();
		refresh_information();
		break;
	    case ID_HANDLER:
		lasthandler++;
		if (filesystems[lasthandler] == NULL)
		    lasthandler = 0;

		if (filesystems[lasthandler])
		    strcpy(handler_name, filesystems[lasthandler]);

		handler = get_filesystems(handler_name);

		if ((handler < 0) || (handler >= MAX_BFFSs) ||
		    (filesystems[handler] == NULL)) {
			handler = lasthandler;
		} else
			lasthandler = handler;

		refresh_handlers();

		if (filesystems[0] == NULL)
			errorexit("No BFFS filesystem handlers left in system\n");

		refresh_information();
		break;
	    case ID_PATH:
		*stat->unix_paths = !*stat->unix_paths;
		refresh_prealloc();
		break;
	    case ID_RESOLVE:
		*stat->resolve_symlinks = !*stat->resolve_symlinks;
		refresh_prealloc();
		break;
	    case ID_CASE:
		*stat->case_independent = !*stat->case_independent;
		refresh_prealloc();
		break;
	    case ID_RONLY:
		superblock->fs_ronly = !superblock->fs_ronly;
		refresh_prealloc();
		break;
	    case ID_COMMENT:
		*stat->link_comments = !*stat->link_comments;
		refresh_prealloc();
		break;
	    case ID_COMMENT2:
		*stat->inode_comments = !*stat->inode_comments;
		refresh_prealloc();
		break;
	    case ID_MINFREE:
		*stat->minfree = !*stat->minfree;
		refresh_prealloc();
		break;
	    case ID_OGINVERT:
		*stat->og_perm_invert = !*stat->og_perm_invert;
		refresh_prealloc();
		break;
	    case ID_UDGAD:
		if ((*stat->cache_cg_size) < CACHE_CG_MAX)
			(*stat->cache_cg_size)++;
		refresh_udgad_info();
		break;
	    case ID_UDGAD + 1:
		if ((*stat->cache_cg_size) > 1)
			(*stat->cache_cg_size)--;
		refresh_udgad_info();
		break;
	    case ID_UDGAD + 2:
		(*stat->cache_size)++;
		refresh_udgad_info();
		break;
	    case ID_UDGAD + 3:
		if ((*stat->cache_size) > 4)
			(*stat->cache_size)--;
		refresh_udgad_info();
		break;
	    case ID_UDGAD + 4:
		if ((*stat->timer_secs) < 30)
			(*stat->timer_secs)++;
		refresh_udgad_info();
		break;
	    case ID_UDGAD + 5:
		if ((*stat->timer_secs) > 0)
			(*stat->timer_secs)--;
		refresh_udgad_info();
		break;
	    case ID_UDGAD + 6:
		if ((*stat->timer_loops) < 60)
			(*stat->timer_loops)++;
		refresh_udgad_info();
		break;
	    case ID_UDGAD + 7:
		if ((*stat->timer_loops) > 0)
			(*stat->timer_loops)--;
		refresh_udgad_info();
		break;
	    case ID_UDGAD + 8:
		if ((*stat->GMT) < 12)
			(*stat->GMT)++;
		refresh_udgad_info();
		refresh_prealloc();
		break;
	    case ID_UDGAD + 9:
		if ((*stat->GMT) > -12)
			(*stat->GMT)--;
		refresh_udgad_info();
		refresh_prealloc();
		break;
	    default:
		printf("got message for unknown gadget %d\n",
			gadget->GadgetID);
	}
}

static void
handle_messages(void)
{
	struct	IntuiMessage *message;
	struct	Gadget *gadget;
	ULONG	class;
	UWORD	code;

	while(1) {
	    WaitPort(window->UserPort);
	    while (message = GT_GetIMsg(window->UserPort)) {
		class	= message->Class;
		code	= message->Code;
		gadget	= (struct Gadget *) message->IAddress;
		GT_ReplyIMsg(message);
		switch (class) {
		    case CLOSEWINDOW:
			cleanup();
			exit(0);
		    case GADGETUP:
			handle_gadget(gadget, code);
			break;
		    case REFRESHWINDOW:
			GT_BeginRefresh(window);
			GT_EndRefresh(window, TRUE);
			break;
		    default:
			printf("got unknown message class %d\n", class);
		}
	    }
	}
}

static void
calc_prealloc(void)
{
	prealloc = (*stat->resolve_symlinks ?  1 : 0 ) |
		   (*stat->case_independent ?  2 : 0 ) |
		   (*stat->unix_paths       ?  4 : 0 ) |
		   (*stat->link_comments    ?  8 : 0 ) |
		   (*stat->inode_comments   ? 16 : 0 ) |
		   (*stat->og_perm_invert   ? 32 : 0 ) |
		   (*stat->minfree          ? 64 : 0 ) |
		   (((unsigned char) *stat->GMT) << 8);
	sprintf(prealloc_str, "0x%x", prealloc);
}

static void
refresh_prealloc(void)
{
	calc_prealloc();
	GT_SetGadgetAttrs(preallocgad, window, NULL, GTTX_Text,
			  prealloc_str, TAG_DONE);

	GT_RefreshWindow(window, NULL);
}

static void
setup(void)
{
	screen = LockPubScreen(NULL);
	if (!screen)
		errorexit("Unable to get lock on public screen!\n");

	visual = GetVisualInfoA(screen, NULL);
	if (!visual)
		errorexit("Couldn't get visual info for public screen\n");

	window = OpenWindowTags(&window_ops, WA_PubScreen, screen, TAG_DONE);
	if (!window)
		errorexit("Unable to create window on public screen!\n");
}

int
main(int argc, char *argv[])
{
	int index;

	handler_name[0] = '\0';

	if (argc < 2) {
	    get_filesystems(0);
	    if (filesystems[0])
		strcpy(handler_name, filesystems[0]);
	    else
		errorexit("There are no BFFS filesystem handlers running\n");
	} else {
	    strcpy(handler_name, argv[1]);
	    index = get_filesystems(handler_name);
	    if ((index < 0) || (index >= MAX_BFFSs)) {
		fprintf(stderr, "%s ", argv[1]);
		errorexit("does not have a mounted BFFS filesystem\n");
	    }
	    if (filesystems[index])
		strcpy(handler_name, filesystems[index]);
	    else {
		fprintf(stderr, "Unable to find %s ", argv[1]);
		errorexit("as a mounted BFFS filesystem\n");
	    }
	}

	if (open_handler(handler_name)) {
	    fprintf(stderr, "Error communicating with %s ", handler_name);
	    errorexit("handler.\n");
	}

	stat = get_stat();
	if (!stat) {
	    if (*handler_name == '\0')
		errorexit("You must specify a BFFS filesystem handler to monitor\n");
	    fprintf(stderr, "%s ", handler_name);
	    errorexit("does not have a mounted BFFS filesystem!\n");
	} else
	    stats = (ULONG *) stat;

	setup();
	setup_gadgets();
	refresh_handlers();
	handle_messages();
	cleanup();
}

