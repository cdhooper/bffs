/* edumpfs.c version 1.4
 *      This program is copyright 1994 - 1996 Chris Hooper.  All code herein
 *      is freeware.  No portion of this code may be sold for profit.
 */
#include <dos/filehandler.h>
#include <exec/memory.h>
#include <stdio.h>
#include <fcntl.h>

#include "fsmacros.h"
#include "convert.h"


#include "ufs.h"
#include "superblock.h"
#include "ufs/sun_label.h"
#include "ufs/bsd_label.h"

char *version = "\0$VER: edumpfs 1.4 (20.Oct.96) © 1996 Chris Hooper";

char	*strchr();

extern int DEV_BSIZE;

char	*progname;
char	*devname;
int	dev_bsize = DEV_BSIZE;
int	sectorsize = DEV_BSIZE;
int	ifd = -1;
int	ofd = -1;
int	fpb = 4;
struct	fs   *superblock = NULL;
struct	fs   *temp_sb = NULL;
struct	csum *cinfo = NULL;
struct	cg   *cg = NULL;
void	break_abort();
ULONG	sb_offset[MAX_PART];
int	active[MAX_PART];
int	read_label	= 1;
int	show_label	= 1;
int	show_superblock	= 1;
int	show_cg_summary	= 1;
int	short_inodes	= 1;
int	short_frags	= 1;
int	sbsize;
int	convert		= 0;
int	invert		= 0;
ULONG	disk16();
ULONG	disk32();
ULONG	disk64();

main(argc, argv)
int	argc;
char	*argv[];
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
		if (*argv[index] == '-') {
			for (argv[index]++; *argv[index] != '\0'; argv[index]++)
				if (*argv[index] == 'l')
					read_label = !read_label;
				else if (*argv[index] == 'b')
					show_label = !show_label;
				else if (*argv[index] == 's')
					show_superblock = !show_superblock;
				else if (*argv[index] == 'c')
					show_cg_summary = !show_cg_summary;
				else if (*argv[index] == 'i')
					short_inodes = !short_inodes;
				else if (*argv[index] == 'I')
					invert = !invert;
				else if (*argv[index] == 'f')
					short_frags = !short_frags;
				else if (*argv[index] == 'h')
					print_help();
				else {
					fprintf(stderr, "unknown option %c\n",
						*argv[index]);
					print_usage();
				}
			continue;
		}

		devname = argv[index];

		colonpos = strchr(devname, ':');
		if (!colonpos || *(colonpos + 1) != '\0')
			ifd = open(devname, O_RDONLY);

		if (ifd < 0)
		    if (dio_open(devname))
			continue;

		if (read_label) {
			print_sun_disk_label();
			print_bsd44_disk_label();
		}

		for (partition = 0; partition < MAX_PART; partition++)
			if (active[partition])
				break;

		if (partition == MAX_PART)
			active[0] = 1;

		if (colonpos)
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
}

print_filesystem(partition)
int partition;
{
	if (active[partition] &&
	    read_superblock(partition, sb_offset[partition])) {

		if (show_superblock)
			print_superblock(partition, sb_offset[partition]);
		if (show_cg_summary)
			print_cg_summary();

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

int print_sun_disk_label()
{
	int	times = 0;
	int	pnum;
	int	bcfact;
	struct	sun_label *label;	/* disk partition table */

	label = (struct sun_label *) AllocMem(DEV_BSIZE,
					      MEMF_PUBLIC | MEMF_CLEAR);

	bread(label, BOOT_BLOCK * DEV_BSIZE / DEV_BSIZE, DEV_BSIZE);

	if (label->bb_magic != SunBB_MAGIC) {
	    if (DISK16(label->bb_magic) != SunBB_MAGIC) {
		bread(label, 1, DEV_BSIZE);
		if (DISK16(label->bb_magic) != SunBB_MAGIC) {
		    FreeMem(label, DEV_BSIZE);
		    return(-1);
		}
	    }
	    convert = 1;
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
		return(0);
	}

	printf(" [Sun %s disk label] ", (convert ? "x86" : ""));

	printf("%s\n", label->bb_mfg_label);
	printf("    Cylinders=%-5d Reserved=%-2d Available=%-5d Media Interleave=%d\n",
		disk16(label->bb_ncyl), disk16(label->bb_ncylres),
		disk16(label->bb_nfscyl), disk16(label->bb_hw_interleave));
	printf("Sectors/Track=%-5d Heads=%-2d    Revs/Min=%-5d  Extra=(%d/%d/%d)\n\n",
		disk16(label->bb_nspt), disk16(label->bb_heads),
		disk16(label->bb_rpm), disk32(label->bb_reserved1),
		disk32(label->bb_reserved2), disk32(label->bb_reserved3));

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
	return(0);
}

int print_bsd44_disk_label()
{
	int	times = 0;
	int	pnum;
	int	fs_offset;
	int	fs_size;
	ULONG	*buffer;
	struct	bsd44_label *label;	/* disk partition table */
	char	buf[32];

	buffer = (ULONG *) AllocMem(DEV_BSIZE, MEMF_PUBLIC | MEMF_CLEAR);

	bread(buffer, BOOT_BLOCK * DEV_BSIZE / DEV_BSIZE, DEV_BSIZE);

	for (fs_offset = 0; fs_offset < DEV_BSIZE / sizeof(ULONG); fs_offset++) {
		label = (struct bsd44_label *) (buffer + fs_offset);
                if ((label->bb_magic  == B44BB_MAGIC) &&
                    (label->bb_magic2 == B44BB_MAGIC))
			goto label_good;
	}

	FreeMem(buffer, DEV_BSIZE);
	return(-1);

	label_good:

	for (pnum = 0; pnum < MAX_PART; pnum++)
	    if (label->bb_part[pnum].fs_size != 0) {
		active[pnum] = 1;
		sb_offset[pnum] = disk32(label->bb_part[pnum].fs_start_sec);
	    }

	if (!show_label) {
		FreeMem(buffer, DEV_BSIZE);
		return(0);
	}

	printf(" [BSD44 disk label] %.16s %.16s",
		label->bb_mfg_label, label->bb_packname);
	printf(" %d %d\n", label->bb_dtype, label->bb_dsubtype);
	printf("       Cylinders=%-4d    Cyl Reserved=%-3d          Spares/Track=%d\n",
		label->bb_ncyl, label->bb_spare_cyl, label->bb_spare_spt);
	printf(" Tracks/Cyl¡nder=%-3d    Total Sectors=%-7d   Spares/Cylinder=%d\n",
		label->bb_heads, label->bb_nsec, label->bb_spare_spc);
	printf("   Sectors/Track=%-3d Sectors/Cylinder=%-5d         Sector Size=%d\n",
		label->bb_nspt, label->bb_nspc, label->bb_secsize);
	printf(" Track Seek Time=%-5d     Track Skew=%-5d Hardware Interleave=%d\n",
		label->bb_trkseek, label->bb_trackskew, label->bb_hw_interleave);
	printf("Head Switch Time=%-5d  Cylinder Skew=%-5d    Disk Revs/Minute=%d\n",
		label->bb_headswitch, label->bb_cylskew, label->bb_rpm);

	printf("\n Part  Start Blk  Num Blks   # MB  FSize  BSize     Start      End\n");
	for (pnum = 0; pnum < MAX_PART; pnum++)
	    if (disk32(label->bb_part[pnum].fs_size) != 0) {
		fs_offset = disk32(label->bb_part[pnum].fs_start_sec);
		fs_size   = disk32(label->bb_part[pnum].fs_size);
		printf("%2d(%c)  %9d %9d ",
			pnum, pnum + 'a', fs_offset, fs_size);
		sprintf(buf, "%d.%03d",
			fs_size * label->bb_secsize / 1048576,
			(fs_size * label->bb_secsize % 1048576) / 1000);
		printf(" %-.5s %6d  %5d ", buf,
			label->bb_part[pnum].fs_fsize,
			label->bb_part[pnum].fs_fsize *
				label->bb_part[pnum].fs_frag);
		sprintf(buf, "%5d/%d/%d",
			fs_offset / label->bb_nspc,
			(fs_offset % label->bb_nspc) / label->bb_nspt,
			fs_offset % label->bb_nspt);
		printf("%-10s%5d/%d/%d\n", buf,
			(fs_offset + fs_size) / label->bb_nspc,
			((fs_offset + fs_size) % label->bb_nspc) / label->bb_nspt,
			(fs_offset + fs_size) % label->bb_nspt);
	    }

	FreeMem(buffer, DEV_BSIZE);
	return(0);
}

print_superblock(partition, offset)
int	partition;
ULONG	offset;
{
	ULONG fs_size;
	char buf[10];

	printf("\nPartition %d at offset %d: ", partition, offset);

	printf("%s\n", superblock->fs_fsmnt);
	printf(" fsize=%-5d  fshift=%-2d  fmask=%08x    cblkno=%-4d  sblkno=%-4d\n",
		disk32(superblock->fs_fsize), disk32(superblock->fs_fshift),
		disk32(superblock->fs_fmask), disk32(superblock->fs_cblkno),
		disk32(superblock->fs_sblkno));
	printf(" bsize=%-5d  bshift=%-2d  bmask=%08x    iblkno=%-4d  dblkno=%-4d\n",
		disk32(superblock->fs_bsize), disk32(superblock->fs_bshift),
		disk32(superblock->fs_bmask), disk32(superblock->fs_iblkno),
		disk32(superblock->fs_dblkno));
	printf("cssize=%-5d csshift=%-2d csmask=%08x    csaddr=%-4d    frag=%-4d\n",
		disk32(superblock->fs_cssize), disk32(superblock->fs_csshift),
		disk32(superblock->fs_csmask), disk32(superblock->fs_csaddr),
		disk32(superblock->fs_frag));
	printf("cgsize=%-5d            cgmask=%08x  cgoffset=%-2d fragshift=%-1d\n",
		disk32(superblock->fs_cgsize), disk32(superblock->fs_cgmask),
		disk32(superblock->fs_cgoffset), disk32(superblock->fs_fragshift));
	printf("sbsize=%-5d\n",
		disk32(superblock->fs_sbsize));
	printf("npsect=%-4d   ntrak=%-4d    ncyl=%-4d     size=%-6ddsize=%-6d\n",
		disk32(superblock->fs_npsect), disk32(superblock->fs_ntrak),
		disk32(superblock->fs_ncyl), disk32(superblock->fs_size),
		disk32(superblock->fs_dsize));
	fs_size = disk32(superblock->fs_dsize) * disk32(superblock->fs_fsize);
	printf("   fpg=%-5d    ipg=%-5d    cpg=%-4d                 Size=%d.%03d MB\n",
		disk32(superblock->fs_fpg), disk32(superblock->fs_ipg),
		disk32(superblock->fs_cpg), fs_size / 1048576,
		(fs_size % 1048576) / 1000);
	fs_size = (disk32(superblock->fs_cstotal.cs_nbfree) *
		   disk32(superblock->fs_frag) +
		   disk32(superblock->fs_cstotal.cs_nffree)) *
		  disk32(superblock->fs_fsize);
	printf("  ndir=%-4d  nifree=%-5d nbfree=%-5d  nffree=%-4d   Free=%d.%03d MB\n",
		disk32(superblock->fs_cstotal.cs_ndir),
		disk32(superblock->fs_cstotal.cs_nifree),
		disk32(superblock->fs_cstotal.cs_nbfree),
		disk32(superblock->fs_cstotal.cs_nffree),
		fs_size / 1048576, (fs_size % 1048576) / 1000);
	printf("  nspf=%-4d  maxbpg=%-5d    ngc=%-3d maxcontig=%-1d      time=%-9d\n",
		disk32(superblock->fs_nspf), disk32(superblock->fs_maxbpg),
		disk32(superblock->fs_ncg), disk32(superblock->fs_optim),
		disk32(superblock->fs_time));
	sprintf(buf, "%d%%", disk32(superblock->fs_minfree));
	printf(" inopb=%-4d  nindir=%-5d  optim=%1d     minfree=%3s     rpm=%-4d\n",
		disk32(superblock->fs_inopb), disk32(superblock->fs_nindir),
		disk32(superblock->fs_optim), buf,
		disk32(superblock->fs_rps) * 60, disk32(superblock->fs_rotdelay));
	printf(" nsect=%-4d  interleave=%-2d  trackskew=%-2d  spc=%-3d\n",
		disk32(superblock->fs_nsect), disk32(superblock->fs_interleave),
		disk32(superblock->fs_spc));
	printf(" fmod=%-2d clean=%-2d ronly=%-2d flags=%-2d cgrotor=%-2d\n",
		superblock->fs_fmod, superblock->fs_clean, superblock->fs_ronly,
		superblock->fs_flags, disk32(superblock->fs_cgrotor));
	printf("cpc=%d contigsumsize=%d maxsymlinklen=%d state=%d magic=%x\n",
		disk32(superblock->fs_cpc), disk32(superblock->fs_contigsumsize),
		disk32(superblock->fs_maxsymlinklen),
		disk32(superblock->fs_state), disk32(superblock->fs_magic));
	printf("qbmask=%08x %08x qfmask=%08x %08x postblformat=%d\n",
	       disk32(superblock->fs_qbmask.val[0]),
					disk32(superblock->fs_qbmask.val[1]),
	       disk32(superblock->fs_qfmask.val[0]),
					disk32(superblock->fs_qfmask.val[1]),
		superblock->fs_postblformat, disk32(superblock->fs_nrpos));
	printf("postbloff=%d rotbloff=%d\n",
	       disk32(superblock->fs_postbloff), disk32(superblock->fs_rotbloff));
}

print_cg_summary()
{
	int	index;
	int	index2;
	struct	csum	*ptr;

	cinfo = (struct csum *) AllocMem(disk32(superblock->fs_cssize),
					 MEMF_PUBLIC | MEMF_CLEAR);
	bread(cinfo, disk32(superblock->fs_csaddr), disk32(superblock->fs_cssize));

	cg = (struct cg *) AllocMem(disk32(superblock->fs_cgsize),
				    MEMF_PUBLIC | MEMF_CLEAR);

/* Note there is also struct csum info stored in the cg block */
	printf("\nCG summary information\n");
	ptr = cinfo;
	for (index = 0; index < disk32(superblock->fs_ncg); index++, ptr++) {
		printf("%2d: ndir=%-3d nbfree=%-4d nifree=%-4d nffree=%-4d ",
			index, disk32(ptr->cs_ndir), disk32(ptr->cs_nbfree),
			disk32(ptr->cs_nifree), disk32(ptr->cs_nffree));
		bread(cg, cgtod(superblock, index), disk32(superblock->fs_cgsize));
		printf("ncyl=%d niblk=%d ndblk=%d\n", (long) disk16(cg->cg_ncyl),
			(long) disk16(cg->cg_niblk), disk32(cg->cg_ndblk));
		printf("    frotor=%d irotor=%d btotoff=%d time=%d boff=%d ",
			disk32(cg->cg_btotoff), disk32(cg->cg_frotor),
			disk32(cg->cg_irotor), disk32(cg->cg_time),
			disk32(cg->cg_boff));
		printf("iusedoff=%d\n    freeoff=%d nextfreeoff=%d ",
			disk32(cg->cg_iusedoff), disk32(cg->cg_freeoff),
			disk32(cg->cg_nextfreeoff));
		printf("cgx=%d ", disk32(cg->cg_cgx));
		printf("frsum=");
		for (index2 = 0; index2 < MAXFRAG; index2++) {
			printf("%d ", disk32(cg->cg_frsum[index2]));
		}
		printf("\n    clustersumoff=%d clusteroff=%d nclusterblks=%d\n",
			disk32(cg->cg_clustersumoff), disk32(cg->cg_clusteroff),
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

read_superblock(partition, sboff)
int	partition;
ULONG	sboff;
{
        int csize = 1;

        temp_sb = (struct fs *) AllocMem(DEV_BSIZE, MEMF_PUBLIC);
	superblock = temp_sb;
	dev_bsize = DEV_BSIZE;
	bread(temp_sb, SUPER_BLOCK * DEV_BSIZE / DEV_BSIZE + sboff, DEV_BSIZE);
	sbsize = disk32(temp_sb->fs_sbsize);

	if ((sbsize < 512) || (sbsize > 32768)) {
	    convert = !convert;
	    sbsize = DISK32(sbsize);
	    if ((sbsize < 512) || (sbsize > 32768)) {
		convert = !convert;
		printf("No Superblock at offset %d for partition %d\n",
			sboff, partition);
		FreeMem(temp_sb, DEV_BSIZE);
		superblock = NULL;
		temp_sb = NULL;
		return(0);
	    }
	}

	if (convert)
		printf("x86 architecture superblock\n");

        superblock = (struct fs *) AllocMem(sbsize, MEMF_PUBLIC | MEMF_CLEAR);
	if (superblock == NULL) {
		printf("unable to allocate memory\n");
		return(0);
	}
	bread(superblock, SUPER_BLOCK * DEV_BSIZE / DEV_BSIZE + sboff, sbsize);
	dev_bsize = disk32(superblock->fs_fsize);
	FreeMem(temp_sb, DEV_BSIZE);
	temp_sb = NULL;

	if (disk32(superblock->fs_magic) != FS_MAGIC) {
		unrecognized:
		printf("Unrecognized Superblock at offset %d for partition %d\n",
			sboff, partition);
		FreeMem(superblock, sbsize);
		superblock = NULL;
		return(0);
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
		return(0);
	}

	fpb = disk32(superblock->fs_frag);
	return(1);
}

char pbuf[128];

pbits(cp, max)
register char *cp;
int max;
{
	int i;
	int count = 0, j;
	int strl = 0;

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

binbits(ptr, num)
register char *ptr;
int num;
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

void break_abort()
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
	dio_close();
	exit(1);
}

print_usage()
{
	fprintf(stderr, "%s\n", version + 7);
	fprintf(stderr, "Usage: %s [options] device:[partition]\n", progname);
	fprintf(stderr, "\t\t-l  do not read boot label information\n");
	fprintf(stderr, "\t\t-b  do not show boot label information\n");
	fprintf(stderr, "\t\t-s  do not show superblock information\n");
	fprintf(stderr, "\t\t-c  do not show cylinder summary information\n");
	fprintf(stderr, "\t\t-i  show bit detail of inodes available\n");
	fprintf(stderr, "\t\t-f  show bit detail of frags available\n");
	fprintf(stderr, "\t\t-I  invert meaning of displayed bits\n");
	fprintf(stderr, "\t\t-h  give more help\n");
	exit(1);
}

print_help()
{
	fprintf(stderr, "%s is a program which is used to display UNIX filesystem and disk\n", progname);
	fprintf(stderr, "label information.  It does this by directly reading the media of the\n");
	fprintf(stderr, "specified mounted partition.  If %s finds a disk label, it will\n", progname);
	fprintf(stderr, "attempt to print filesystem information pertaining to each partition\n");
	fprintf(stderr, "listed in the disk label.  %s currently understands SunOS type\n", progname);
	fprintf(stderr, "as well as the new BSD disk label (which can be created and edited with\n");
	fprintf(stderr, "diskpart).  There is currently no facility for editing a SunOS disk\n");
	fprintf(stderr, "label (other than using a Sun to do it).\n");

	fprintf(stderr, "\nExample:  %s BSDR:\n", progname);
	fprintf(stderr, "\t\tWill print all disk label, superblock, and cylinder group\n");
	fprintf(stderr, "\t\tinformation available within AmigaDOS device BSDR:\n");
	fprintf(stderr, "Example:  %s -c BSD:b\n", progname);
	fprintf(stderr, "\t\tWill print the second partition defined for device BSD:\n");
	fprintf(stderr, "\t\t(excluding cylinder summary information)\n");
	exit(0);
}


ULONG disk16(x)
ULONG x;
{
	if (convert)
		return((ULONG) DISK16(x));
	else
		return(x);
}


ULONG disk32(x)
ULONG x;
{
	if (convert)
		return(DISK32(x));
	else
		return(x);
}


ULONG disk64(x)
quad x;
{
	if (convert)
		return(DISK64(x));
	else
		return(x.val[0]);
}
