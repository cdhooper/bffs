/* edumpfs
 *      This program is copyright (1994-2018) Chris Hooper.  All code herein
 *      is freeware.  No portion of this code may be sold for profit.
 */

const char *version = "\0$VER: edumpfs 1.5 (19-Jan-2018) © Chris Hooper";

#include <dos/filehandler.h>
#include <exec/memory.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <clib/exec_protos.h>

#include "fsmacros.h"
#include "convert.h"

#include "ufs.h"
#include "superblock.h"
#include "ufs/sun_label.h"
#include "ufs/bsd_label.h"
#include <amiga_device.h>

extern int DEV_BSIZE;

char	*progname;
char	*devname = NULL;
int	dev_bsize = DEV_BSIZE;
int	sectorsize = DEV_BSIZE;
int	ifd = -1;
int	ofd = -1;
int	fpb = 4;
struct	fs   *superblock = NULL;
struct	fs   *temp_sb = NULL;
struct	csum *cinfo = NULL;
struct	cg   *cg = NULL;
ULONG	sb_offset[MAX_PART];
int	active[MAX_PART];
int	read_label	= 1;
int	show_label	= 1;
int	show_superblock	= 1;
int	show_cg_summary	= 1;
int	short_inodes	= 1;
int	short_frags	= 1;
int	sbsize;
int	swap_endian	= 0;
int	invert		= 0;
int	super_block	= SUPER_BLOCK;

static ULONG
disk16(ULONG x)
{
	if (swap_endian)
		return ((ULONG) DISK16(x));
	else
		return (x);
}


static ULONG
disk32(ULONG x)
{
	if (swap_endian)
		return (DISK32(x));
	else
		return (x);
}


static ULONG
disk64(quad x)
{
	if (swap_endian)
		return (DISK64(x));
	else
		return (x.val[0]);
}

static void
print_superblock(int partition, ULONG offset)
{
	ULONG temp_size;
	ULONG temp_size2;
	int   fs_size_shift = 0;
	char buf[10];

	printf("\nPartition %d at offset %d: %s\n",
	       partition, offset, superblock->fs_fsmnt);
	printf(" fsize=%-6d fshift=%-3d fmask=%08x    cblkno=%-5d  sblkno=%-4d\n",
		disk32(superblock->fs_fsize), disk32(superblock->fs_fshift),
		disk32(superblock->fs_fmask), disk32(superblock->fs_cblkno),
		disk32(superblock->fs_sblkno));
	printf(" bsize=%-6d bshift=%-3d bmask=%08x    iblkno=%-5d  dblkno=%-4d\n",
		disk32(superblock->fs_bsize), disk32(superblock->fs_bshift),
		disk32(superblock->fs_bmask), disk32(superblock->fs_iblkno),
		disk32(superblock->fs_dblkno));
	printf("cssize=%-5d csshift=%-2d csmask=%08x    csaddr=%-7d  frag=%-4d\n",
		disk32(superblock->fs_cssize), disk32(superblock->fs_csshift),
		disk32(superblock->fs_csmask), disk32(superblock->fs_csaddr),
		disk32(superblock->fs_frag));
	printf("cgsize=%-5d            cgmask=%08x  cgoffset=%-2d fragshift=%-1d\n",
		disk32(superblock->fs_cgsize), disk32(superblock->fs_cgmask),
		disk32(superblock->fs_cgoffset), disk32(superblock->fs_fragshift));
	printf("sbsize=%-6d altsb=%d\n",
		disk32(superblock->fs_sbsize), cgsblock(superblock, disk32(superblock->fs_ncg) - 1));
	printf("npsect=%-6d ntrak=%-7d ncyl=%-7d size=%-8d\n",
		disk32(superblock->fs_npsect),
		disk32(superblock->fs_ntrak),
		disk32(superblock->fs_ncyl),
		disk32(superblock->fs_size));
	temp_size = disk32(superblock->fs_dsize) / 10;
	fs_size_shift = disk32(superblock->fs_fshift);
	if (fs_size_shift > 10)
	    temp_size <<= (fs_size_shift - 10);
	else
	    temp_size >>= (10 - fs_size_shift);
	printf(" nsect=%-7d nspf=%-8d cpg=%-6d dsize=%-8d Size=%u.%02u MB\n",
		disk32(superblock->fs_nsect),
		disk32(superblock->fs_nspf),
		disk32(superblock->fs_cpg),
		disk32(superblock->fs_dsize),
		temp_size / 100, temp_size % 100);

	temp_size = disk32(superblock->fs_cstotal.cs_nffree);
	if (fs_size_shift > 10)
	    temp_size <<= (fs_size_shift - 10);
	else
	    temp_size >>= (10 - fs_size_shift);

	temp_size2 = disk32(superblock->fs_cstotal.cs_nbfree);
	fs_size_shift = disk32(superblock->fs_bshift);
	if (fs_size_shift > 10)
	    temp_size2 <<= (fs_size_shift - 10);
	else
	    temp_size2 >>= (10 - fs_size_shift);

	temp_size += temp_size2;
	temp_size /= 10;
	printf("   fpg=%-8d ipg=%-8d ncg=%-5d nbfree=%-8d Free=%u.%02u MB\n",
		disk32(superblock->fs_fpg),
		disk32(superblock->fs_ipg),
		disk32(superblock->fs_ncg),
		disk32(superblock->fs_cstotal.cs_nbfree),
		temp_size / 100, temp_size % 100);


	printf("  ndir=%-6d clean=%-5d maxbpg=%-5d nffree=%-6d\n",
		disk32(superblock->fs_cstotal.cs_ndir),
		superblock->fs_clean,
		disk32(superblock->fs_maxbpg),
		disk32(superblock->fs_cstotal.cs_nffree));
	sprintf(buf, "%d%%", disk32(superblock->fs_minfree));
	printf("   cpc=%-2d maxcontig=%-4d minfree=%-5s nifree=%-6d\n",
		disk32(superblock->fs_cpc),
		disk32(superblock->fs_maxcontig), buf,
		disk32(superblock->fs_cstotal.cs_nifree));
	printf(" inopb=%-5d nindir=%-6d optim=%-6d flags=%02x\n",
		disk32(superblock->fs_inopb),
		disk32(superblock->fs_nindir),
		disk32(superblock->fs_optim),
		superblock->fs_flags);
	printf("   rpm=%-5d intrlv=%-4d trkskew=%-3d rotdelay=%d\n",
		disk32(superblock->fs_rps) * 60,
		disk32(superblock->fs_interleave),
		disk32(superblock->fs_trackskew),
		disk32(superblock->fs_rotdelay));
	printf("   spc=%-6d ronly=%-4d cgrotor=%-7d time=%-9d\n",
		disk32(superblock->fs_spc),
		superblock->fs_ronly,
		disk32(superblock->fs_cgrotor),
		disk32(superblock->fs_time));
	printf(" state=%-6d magic=%-11x contigsumsize=%-3d maxsymlinklen=%d\n",
		disk32(superblock->fs_state),
		disk32(superblock->fs_magic),
		disk32(superblock->fs_contigsumsize),
		disk32(superblock->fs_maxsymlinklen));
	printf("qbmask=%08x:%08x qfmask=%08x:%08x  postblformat=%d\n",
	       disk32(superblock->fs_qbmask.val[swap_endian]),
	       disk32(superblock->fs_qbmask.val[!swap_endian]),
	       disk32(superblock->fs_qfmask.val[swap_endian]),
	       disk32(superblock->fs_qfmask.val[!swap_endian]),
	       superblock->fs_postblformat, disk32(superblock->fs_nrpos));
	printf("postbloff=%d rotbloff=%d\n",
	       disk32(superblock->fs_postbloff), disk32(superblock->fs_rotbloff));
	/* Calculate device block size based on the superblock */
	dev_bsize = disk32(superblock->fs_fsize) / disk32(superblock->fs_nspf);
	dio_assign_bsize(dev_bsize);
}

static void
pbits(register char *cp, int max)
{
	int i;
	int count = 0, j;
	int strl = 0;
	static char pbuf[128];

	pbuf[0] = '\0';

	for (i = 0; i < max; i++)
		if ((isset(cp, i) && !invert) ||
		    (isclr(cp, i) && invert)) {
			if (strl > 62) {
				printf("%s\n          ", pbuf);
				pbuf[0] = '\0';
				strl = 0;
			}
			count++;
			sprintf(pbuf + strl, "%s%d", strl ? "," : "", i);
			strl += strlen(pbuf + strl);
			j = i;
			while ((i+1)<max && ((isset(cp, i+1) && !invert) ||
					     (isclr(cp, i+1) && invert)) )
				i++;
			if (i != j) {
				sprintf(pbuf + strl, "-%d", i);
				strl += strlen(pbuf + strl);
			}
		}
	if (strl)
		printf("%s", pbuf);
	printf("\n");
}

static void
binbits(register char *ptr, int num)
{
	int index;
	int lpos = 11;
	for (index = 0; index < num; index++) {
		if (index && ((index % fpb) == 0))
			if (lpos > 75) {
				printf("\n          ");
				lpos = 11;
			} else {
				printf(" ");
				lpos++;
			}
		if (invert)
			printf("%d", isset(ptr, index) == 0);
		else
			printf("%d", isset(ptr, index) != 0);
		lpos++;
	}
	printf("\n");
}

static void
print_cg_summary(ULONG poffset)
{
	int	index;
	int	index2;
	struct	csum	*ptr;
	ULONG   blk;

	cinfo = (struct csum *) AllocMem(disk32(superblock->fs_cssize),
					 MEMF_PUBLIC | MEMF_CLEAR);
	if (bread((char *) cinfo, disk32(superblock->fs_csaddr) + poffset,
		  disk32(superblock->fs_cssize))) {
	    printf("Read blk %u failed\n", disk32(superblock->fs_csaddr));
	    return;
	}

	cg = (struct cg *) AllocMem(disk32(superblock->fs_cgsize),
				    MEMF_PUBLIC | MEMF_CLEAR);

/* Note there is also struct csum info stored in the cg block */
	printf("\nCG summary information\n");
	ptr = cinfo;
	for (index = 0; index < disk32(superblock->fs_ncg); index++, ptr++) {
		printf("%2d: ndir=%-3d nbfree=%-4d nifree=%-4d nffree=%-4d ",
			index, disk32(ptr->cs_ndir), disk32(ptr->cs_nbfree),
			disk32(ptr->cs_nifree), disk32(ptr->cs_nffree));
		blk = fsbtodb(superblock, cgtod(superblock, index));
		if (bread((char *) cg, blk + poffset,
			disk32(superblock->fs_cgsize))) {
		    printf("Read blk %u failed\n", cgtod(superblock, index));
		    return;
		}
		printf("ncyl=%d niblk=%d ndblk=%d\n",
			(long) disk16(cg->cg_ncyl),
			(long) disk16(cg->cg_niblk),
			disk32(cg->cg_ndblk));
		printf("    frotor=%d irotor=%d btotoff=%d time=%d boff=%d ",
			disk32(cg->cg_btotoff),
			disk32(cg->cg_frotor),
			disk32(cg->cg_irotor),
			disk32(cg->cg_time),
			disk32(cg->cg_boff));
		printf("iusedoff=%d\n    freeoff=%d nextfreeoff=%d ",
			disk32(cg->cg_iusedoff),
			disk32(cg->cg_freeoff),
			disk32(cg->cg_nextfreeoff));
		printf("cgx=%d ", disk32(cg->cg_cgx));
		printf("frsum=");
		for (index2 = 0; index2 < MAXFRAG; index2++) {
			printf("%d ", disk32(cg->cg_frsum[index2]));
		}
		printf("\n    clustersumoff=%d clusteroff=%d nclusterblks=%d\n",
			disk32(cg->cg_clustersumoff),
			disk32(cg->cg_clusteroff),
			disk32(cg->cg_nclusterblks));

		if (invert)
			printf("    ifree=");
		else
			printf("    iused=");
		if (short_inodes)
			pbits((char *) cg + disk32(cg->cg_iusedoff),
				disk32(superblock->fs_ipg));
		else
			binbits((char *) cg + disk32(cg->cg_iusedoff),
				disk32(superblock->fs_ipg));

		if (invert)
			printf("    fused=");
		else
			printf("    ffree=");

		if (short_frags)
			pbits((char *) cg + disk32(cg->cg_freeoff),
				disk32(superblock->fs_fpg));
		else
			binbits((char *) cg + disk32(cg->cg_freeoff),
				disk32(superblock->fs_fpg));
		printf("\n");
	}
}

static int
read_superblock(int partition, ULONG sboff)
{
        int csize = 1;

        temp_sb = (struct fs *) AllocMem(DEV_BSIZE, MEMF_PUBLIC);
	superblock = temp_sb;
	dev_bsize = DEV_BSIZE;
	if (bread((char *)temp_sb, super_block * 512 / DEV_BSIZE + sboff,
		    DEV_BSIZE)) {
	    printf("Read blk %u failed\n",
		    super_block * 512 / DEV_BSIZE + sboff);
	    return (0);
	}
	sbsize = disk32(temp_sb->fs_sbsize);

	if ((sbsize < 512) || (sbsize > 32768)) {
	    swap_endian = !swap_endian;
	    sbsize = DISK32(sbsize);
	    if ((sbsize < 512) || (sbsize > 32768)) {
		swap_endian = !swap_endian;
		printf("No Superblock at offset %d for partition %d\n",
			super_block * 512 / DEV_BSIZE + sboff, partition);
		FreeMem(temp_sb, DEV_BSIZE);
		superblock = NULL;
		temp_sb = NULL;
		return (0);
	    }
	}

	if (swap_endian)
		printf("x86 architecture superblock\n");

        superblock = (struct fs *) AllocMem(sbsize, MEMF_PUBLIC | MEMF_CLEAR);
	if (superblock == NULL) {
		printf("unable to allocate memory\n");
		return (0);
	}
	if (bread((char *)superblock,
		    super_block * 512 / DEV_BSIZE + sboff, sbsize)) {
	    printf("Read blk %u failed\n",
		    super_block * 512 / DEV_BSIZE + sboff, sbsize);
	    return (0);
	}
	dev_bsize = disk32(superblock->fs_fsize);
	FreeMem(temp_sb, DEV_BSIZE);
	temp_sb = NULL;

	if ((disk32(superblock->fs_magic) != FS_UFS1_MAGIC) &&
	    (disk32(superblock->fs_magic) != FS_UFS2_MAGIC)) {
		unrecognized:
		printf("Unrecognized Superblock at offset %d for partition %d\n",
			sboff, partition);
		FreeMem(superblock, sbsize);
		superblock = NULL;
		return (0);
	}

	if ((disk32(superblock->fs_ncg) > 4096) ||
	    (disk32(superblock->fs_fsize) & 511) ||
	    (disk32(superblock->fs_bsize) & 4095) ||
	    ((disk32(superblock->fs_fsize) *
	      disk32(superblock->fs_frag)) !=
	     disk32(superblock->fs_bsize))) {
		printf("Bad values in superblock at offset %d for partition %d\n",
			sboff, partition);
		FreeMem(superblock, sbsize);
		superblock = NULL;
		return (0);
	}

	fpb = disk32(superblock->fs_frag);
	return (1);
}

static void
print_filesystem(int partition)
{
	if (active[partition] &&
	    read_superblock(partition, sb_offset[partition])) {

		if (show_superblock)
			print_superblock(partition, sb_offset[partition]);
		if (show_cg_summary)
			print_cg_summary(sb_offset[partition]);

		if (cinfo) {
			FreeMem(cinfo, disk32(superblock->fs_cssize));
			cinfo = NULL;
		}
		if (cg) {
			FreeMem(cg, disk32(superblock->fs_cgsize));
			cg = NULL;
		}
		FreeMem(superblock, sbsize);
		superblock = NULL;
	}
}

static int
print_sun_disk_label(void)
{
	int	times = 0;
	int	pnum;
	int	bcfact;
	struct	sun_label *label;	/* disk partition table */

	label = (struct sun_label *) AllocMem(DEV_BSIZE,
					      MEMF_PUBLIC | MEMF_CLEAR);

	if (bread((char *) label, BOOT_BLOCK * DEV_BSIZE / DEV_BSIZE,
		    DEV_BSIZE)) {
	    printf("Read blk %u failed\n", BOOT_BLOCK * DEV_BSIZE / DEV_BSIZE);
	    return (-1);
	}

	if (label->bb_magic != SunBB_MAGIC) {
	    if (DISK16(label->bb_magic) != SunBB_MAGIC) {
		if (bread((char *) label, 1, DEV_BSIZE)) {
		    printf("Read blk %u failed\n", 1);
		    return (-1);
		}
		if (DISK16(label->bb_magic) != SunBB_MAGIC) {
		    FreeMem(label, DEV_BSIZE);
		    return (-1);
		}
	    }
	    swap_endian = 1;
	}

	bcfact = disk16(label->bb_heads) * disk16(label->bb_nspt);

	for (pnum = 0; pnum < MAX_PART; pnum++)
	    if (disk32(label->bb_part[pnum].fs_size) != 0) {
		active[pnum] = 1;
		sb_offset[pnum] = disk32(label->bb_part[pnum].fs_start_cyl) *
				  bcfact;
	    }

	if (!show_label) {
		FreeMem(label, DEV_BSIZE);
		return (0);
	}

	printf(" [Sun%s disk label] ", (swap_endian ? " x86" : ""));

	printf("%s\n", label->bb_mfg_label);
	printf("    Cylinders=%-5d Reserved=%-2d Available=%-5d Media Interleave=%d\n",
		disk16(label->bb_ncyl), disk16(label->bb_ncylres),
		disk16(label->bb_nfscyl), disk16(label->bb_hw_interleave));
	printf("Sectors/Track=%-5d Heads=%-2d    Revs/Min=%-5d  "
	       "Altsect/Cyl=%-5d   Extra=(%d/%d/%d)\n\n",
		disk16(label->bb_nspt), disk16(label->bb_heads),
		disk16(label->bb_rpm), disk32(label->bb_reserved1),
		disk16(label->bb_alt_per_cyl),
		disk16(label->bb_reserved2), disk32(label->bb_reserved3));

	printf("Partition   Start (Cyl/Block)   Next Available (Cyl/Block)\n");
	for (pnum = 0; pnum < MAX_PART; pnum++)
	    if (label->bb_part[pnum].fs_size != 0) {
		printf("%4d(%c)     %7d/%-9d     %7d/%-9d\n",
			pnum, pnum + 'a',
			disk32(label->bb_part[pnum].fs_start_cyl),

			disk32(label->bb_part[pnum].fs_start_cyl) * bcfact,

			disk32(label->bb_part[pnum].fs_size) / bcfact +
			disk32(label->bb_part[pnum].fs_start_cyl),

			disk32(label->bb_part[pnum].fs_size) +
			disk32(label->bb_part[pnum].fs_start_cyl) * bcfact);
		times++;
	    }

	FreeMem(label, DEV_BSIZE);
	return (0);
}

static int
print_bsd44_disk_label(void)
{
	int	times = 0;
	int	pnum;
	int	fs_offset;
	ULONG	fs_size;
	ULONG   temp_size;
	ULONG   temp_size2;
	int     fs_size_shift = 0;
	ULONG	*buffer;
	struct	bsd44_label *label;	/* disk partition table */
	char	buf[32];

	buffer = (ULONG *) AllocMem(DEV_BSIZE, MEMF_PUBLIC | MEMF_CLEAR);

	if (bread((char *) buffer, BOOT_BLOCK * DEV_BSIZE / DEV_BSIZE,
		    DEV_BSIZE)) {
	    printf("Read blk %u failed\n", BOOT_BLOCK * DEV_BSIZE / DEV_BSIZE);
	    return (-1);
	}

	for (fs_offset = 0; fs_offset < DEV_BSIZE / sizeof (ULONG); fs_offset++) {
		label = (struct bsd44_label *) (buffer + fs_offset);
                if ((label->bb_magic  == B44BB_MAGIC) &&
                    (label->bb_magic2 == B44BB_MAGIC))
			goto label_good;
	}

	FreeMem(buffer, DEV_BSIZE);
	return (-1);

	label_good:

	for (pnum = 0; pnum < MAX_PART; pnum++)
	    if (label->bb_part[pnum].fs_size != 0) {
		active[pnum] = 1;
		sb_offset[pnum] = disk32(label->bb_part[pnum].fs_start_sec);
	    }

	if (!show_label) {
		FreeMem(buffer, DEV_BSIZE);
		return (0);
	}

	printf(" [BSD44 disk label] %.16s %.16s",
		label->bb_mfg_label, label->bb_packname);
	printf(" %d %d\n", label->bb_dtype, label->bb_dsubtype);
	printf("       Cylinders=%-4d    Cyl Reserved=%-3d          Spares/Track=%d\n",
		label->bb_ncyl, label->bb_spare_cyl, label->bb_spare_spt);
	printf(" Tracks/Cylinder=%-3d    Total Sectors=%-10uSpares/Cylinder=%d\n",
		label->bb_heads, label->bb_nsec, label->bb_spare_spc);
	printf("   Sectors/Track=%-3d Sectors/Cylinder=%-5d         Sector Size=%d\n",
		label->bb_nspt, label->bb_nspc, disk32(label->bb_secsize));
	printf(" Track Seek Time=%-5d     Track Skew=%-5d Hardware Interleave=%d\n",
		label->bb_trkseek, label->bb_trackskew, label->bb_hw_interleave);
	printf("Head Switch Time=%-5d  Cylinder Skew=%-5d    Disk Revs/Minute=%d\n",
		label->bb_headswitch, label->bb_cylskew, label->bb_rpm);

	printf("\n Part  StartBlk  Num_Blks     #_MB FSize BSize      Start        End\n");
	for (pnum = 0; pnum < MAX_PART; pnum++)
	    if (disk32(label->bb_part[pnum].fs_size) != 0) {
		fs_offset = disk32(label->bb_part[pnum].fs_start_sec);
		fs_size   = disk32(label->bb_part[pnum].fs_size);

		printf("%2d(%c) %9d %9d ",
			pnum, pnum + 'a', fs_offset, fs_size);

		fs_size_shift = 0;
		for (temp_size2 = disk32(label->bb_secsize); temp_size2 > 1;
		     temp_size2 >>= 1) {
		    fs_size_shift++;
		}
		temp_size = fs_size;
		if (fs_size_shift > 10)
		    temp_size <<= (fs_size_shift - 10);
		else
		    temp_size >>= (10 - fs_size_shift);
		temp_size /= 10;

		sprintf(buf, "%u.%02u", temp_size / 100, temp_size % 100);
		printf("%8s %5d %5d ", buf,
			label->bb_part[pnum].fs_fsize,
			label->bb_part[pnum].fs_fsize *
				label->bb_part[pnum].fs_frag);
		sprintf(buf, "%d/%d/%d",
			fs_offset / label->bb_nspc,
			(fs_offset % label->bb_nspc) / label->bb_nspt,
			fs_offset % label->bb_nspt);
		printf("%10s ", buf);
		sprintf(buf, "%d/%d/%d",
			(fs_offset + fs_size) / label->bb_nspc,
			((fs_offset + fs_size) % label->bb_nspc) / label->bb_nspt,
			(fs_offset + fs_size) % label->bb_nspt);
		printf("%10s\n", buf);
	    }

	/* Calculate device block size based on the partition table */
	dev_bsize = disk32(label->bb_secsize);

	FreeMem(buffer, DEV_BSIZE);
	dio_assign_bsize(dev_bsize);
	return (0);
}

void
error_exit(int rc)
{
	dio_close();
	exit(rc);
}

static int
break_abort(void)
{
	static int entered = 0;

	if (entered++)
		return;

	if (temp_sb)
		FreeMem(temp_sb, DEV_BSIZE);

	if (superblock && (superblock != temp_sb)) {
		if (cinfo) {
			FreeMem(cinfo, disk32(superblock->fs_cssize));
			cinfo = NULL;
		}
		if (cg) {
			FreeMem(cg, disk32(superblock->fs_cgsize));
			cg = NULL;
		}
		FreeMem(superblock, sbsize);
	}

	superblock = NULL;
	temp_sb = NULL;
	error_exit(1);
}

static void
print_usage(void)
{
	fprintf(stderr, "%s\n"
	    "Usage: %s [options] device:[partition]\n"
	    "\t\t-b       do not show boot label information\n"
	    "\t\t-c       do not show cylinder summary information\n"
	    "\t\t-f       show bit detail of frags available\n"
	    "\t\t-h       give more help\n"
	    "\t\t-i       show bit detail of inodes available\n"
	    "\t\t-I       invert meaning of displayed bits\n"
	    "\t\t-l       do not read boot label information\n"
	    "\t\t-o <blk> specify partition offset\n"
	    "\t\t-s <blk> specify superblock location\n"
	    "\t\t-S       do not show superblock information\n",
	    version + 7, progname);
	exit(1);
}

static void
print_help(void)
{
	fprintf(stderr,
	    "%s is a program which is used to display UNIX filesystem and disk\n"
	    "label information.  It does this by directly reading the media of the\n"
	    "specified mounted partition.  If %s finds a disk label, it will\n"
	    "attempt to print filesystem information pertaining to each partition\n"
	    "listed in the disk label.  %s currently understands SunOS type\n"
	    "as well as the BSD disk label (which can be created and edited with\n"
	    "diskpart).  There is currently no facility for editing a SunOS disk\n"
	    "label (other than using a Sun to do it).\n"

	    "\nExample:  %s BSDR:\n"
	    "\t\tWill print all disk label, superblock, and cylinder group\n"
	    "\t\tinformation available within AmigaDOS device BSDR:\n"
	    "Example:  %s -c BSD:b\n"
	    "\t\tWill print the second partition defined for device BSD:\n"
	    "\t\t(excluding cylinder summary information)\n",
	    progname, progname, progname, progname, progname);
	exit(0);
}

int
main(int argc, char *argv[])
{
	int	index;
	int	partition;
	char	*colonpos;

	progname = argv[0];

	if (argc == 1)
		print_usage();

	onbreak(break_abort);

	for (partition = 0; partition < MAX_PART; partition++) {
		sb_offset[partition] = 0;
		active[partition] = 0;
	}

	for (index = 1; index < argc; index++) {
		char *ptr = argv[index];
		if (*ptr == '-') {
			for (ptr++; *ptr != '\0'; ptr++) {
				if (*ptr == 'b')
					show_label = !show_label;
				else if (*ptr == 'c')
					show_cg_summary = !show_cg_summary;
				else if (*ptr == 'f')
					short_frags = !short_frags;
				else if (*ptr == 'h')
					print_help();
				else if (*ptr == 'i')
					short_inodes = !short_inodes;
				else if (*ptr == 'I')
					invert = !invert;
				else if (*ptr == 'l')
					read_label = !read_label;
				else if (*ptr == 's') {
					index++;
					if (index >= argc) {
					    fprintf(stderr, "-%c option requires 512-sector number\n", *ptr);
					    print_usage();
					}
					super_block = atoi(argv[index]);
				} else if (*ptr == 'o') {
					index++;
					if (index >= argc) {
					    fprintf(stderr, "-%c option requires block number\n", *ptr);
					    print_usage();
					}
					sb_offset[0] = atoi(argv[index]);
					read_label = 0;
				} else if (*ptr == 'S')
					show_superblock = !show_superblock;
				else {
					fprintf(stderr, "unknown option %c\n",
						*ptr);
					print_usage();
				}
			}
			continue;
		}

		devname = argv[index];

		colonpos = strchr(devname, ':');
		if (!colonpos || *(colonpos + 1) != '\0')
			ifd = open(devname, O_RDONLY);

		if (ifd < 0)
		    if (dio_open(devname)) {
			printf("Failed to open %s\n", devname);
			continue;
		    }

		if (read_label) {
			print_sun_disk_label();
			print_bsd44_disk_label();
		}

		for (partition = 0; partition < MAX_PART; partition++)
			if (active[partition])
				break;

		if (partition == MAX_PART)
			active[0] = 1;

		if (colonpos && colonpos[2] == '\0')
			if (isdigit(colonpos[1]))
				partition = colonpos[1] - '0';
			else
				partition = colonpos[1] - 'a';
		else
			partition = -1;

		if ((partition >= 0) && (partition < MAX_PART))
			print_filesystem(partition);
		else
			for (partition = 0; partition < MAX_PART; partition++)
				print_filesystem(partition);

		dio_close();
	}
	if (devname == NULL)
	    print_usage();
}
