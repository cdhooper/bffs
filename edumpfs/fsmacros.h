#define FRAGS_PER_BLK   superblock->fs_frag

#define fsbtodb(fs, b)  ((b) << (fs)->fs_fsbtodb)

/*
 * Cylinder group macros to locate things in cylinder groups.
 * They calc file system addresses of cylinder group data structures.
 */
#define cgbase(fs, c)   ((daddr_t)(disk32(fs->fs_fpg) * (c)))
#define cgstart(fs, c)  (cgbase(fs, c) + disk32(fs->fs_cgoffset) *      \
                        ((c) & ~(disk32(fs->fs_cgmask))))         /* cg start */
#define cgsblock(fs, c) (cgstart(fs, c) + disk32(fs->fs_sblkno))  /* super blk */
#define cgtod(fs, c)    (cgstart(fs, c) + disk32(fs->fs_cblkno))  /* cg block */
#define cgimin(fs, c)   (cgstart(fs, c) + disk32(fs->fs_iblkno))  /* inode blk */
#define cgdmin(fs, c)   (cgstart(fs, c) + disk32(fs->fs_dblkno))  /* 1st data */

#define blkstofrags(fs, blks)   /* calculates (blks * fs->fs_frag) */   \
        ((blks) << disk32(fs->fs_fragshift))

/* inode number to file system frag offset      */
#define itofo(fs, x)    ((x) & (INOPF(fs)-1))
/* inode number to frag number in block         */
#define itodo(fs, x)    (itoo(fs,x) / INOPF(fs))

#define itoo(fs, x)     ((x) % disk32(fs->fs_inopb))
#define itog(fs, x)     ((x) / disk32(fs->fs_ipg))
#define itod(fs, x)                                                     \
        ((daddr_t)(cgimin(fs, itog(fs, x)) +                            \
        (blkstofrags(fs, (((x) % disk32(fs->fs_ipg)) / disk32(fs->fs_inopb))))))


/*  x % FBSIZE  */
#define blkoff(fs, x)   ((x) & fs_lbmask)

/*  x % FSIZE  */
#define fragoff(fs, x)  ((x) & fs_lfmask)

/*  x / FBSIZE  */
#define lblkno(fs, x)   ((x) >> disk32(fs->fs_bshift))

/*  x / FSIZE  */
#define lfragno(fs, x)  ((x) >> disk32(fs->fs_fshift))

/* Inodes per block */
#define INOPB(fs)       (disk32(fs->fs_inopb))
/* Inodes per frag */
#define INOPF(fs)       (disk32(fs->fs_inopb) >> disk32(fs->fs_fragshift))

#define cg_blksfree(cgp)                                                \
        ((disk32(cgp->cg_magic) != CG_MAGIC)                            \
        ? (disk32(((struct ocg *)(cgp))->cg_free))                      \
        : ((u_char *)((char *)(cgp) + disk32(cgp->cg_freeoff))))

#define cg_chkmagic(cgp)                                                \
        (disk32(cgp->cg_magic) == CG_MAGIC ||                           \
        disk32(((struct ocg *)(cgp))->cg_magic) == CG_MAGIC)

/*
 * Extract the bits for a block from a map.
 * Compute the cylinder and rotational position of a cyl block addr.
 */
#define blkmap(fs, map, loc)                                            \
        (((map)[(loc) / NBBY] >> ((loc) % NBBY)) &                      \
        (0xff >> (NBBY - (fs)->fs_frag)))
#define cbtocylno(fs, bno)                                              \
        ((bno) * NSPF(fs) / disk32((fs)->fs_spc))
#define cbtorpos(fs, bno)                                                \
        (((bno) * NSPF(fs) % disk32(fs->fs_spc) /                        \
        disk32(fs->fs_nsect) * disk32(fs->fs_trackskew) +                \
        (bno) * NSPF(fs) % disk32(fs->fs_spc) %                          \
        disk32(fs->fs_nsect) * disk32(fs->fs_interleave)) %              \
        disk32(fs->fs_nsect) * disk32(fs->fs_nrpos) / disk32(fs->fs_npsect))


/*
 * Number of disk sectors per block; assumes DEV_BSIZE byte sector size.
 */
#define NSPB(fs)        (disk32(fs->fs_nspf << (fs)->fs_fragshift))
#define NSPF(fs)        (disk32(fs->fs_nspf))

/*
 * The following macros optimize certain frequently calculated
 * quantities by using shifts and masks in place of divisions
 * modulos and multiplications.
 */
#define fragroundup(fs, size)   /* calculates roundup(size, fs->fs_fsize) */    \
        (((size) + disk32(fs->fs_fsize - 1)) & ~fs_lfmask)
#define numfrags(fs, loc)       /* calculates (loc / fs->fs_fsize) */           \
        ((loc) >> disk32(fs->fs_fshift))
#define fragnum(fs, fsb)        /* calculates (fsb % fs->fs_frag) */            \
        ((fsb) & (disk32(fs->fs_frag) - 1))
#define blknum(fs, fsb)         /* calculates rounddown(fsb, fs->fs_frag) */    \
        ((fsb) &~ (disk32(fs->fs_frag) - 1))


/*
 * Give cylinder group number for a file system block.
 * Give cylinder group block number for a file system block.
 */
#define dtog(fs, d)     ((d) / disk32(fs->fs_fpg))
#define dtogd(fs, d)    ((d) % disk32(fs->fs_fpg))

/*
 * Convert cylinder group to base address of its global summary info.
 *
 * N.B. This macro assumes that sizeof (struct csum) is a power of two.
 */
#define fs_cs(fs, indx)                                                 \
        fs_csp[(indx) >> disk32(fs->fs_csshift)][(indx) & ~disk32(fs->fs_csmask)]

/*
 * Macros for access to cylinder group array structures
 */
#define cg_inosused(cgp)                                                \
        ((disk32(cgp->cg_magic) != CG_MAGIC)                            \
        ? (((struct ocg *)(cgp))->cg_iused)                             \
        : ((char *)((char *)(cgp) + disk32(cgp->cg_iusedoff))))
#define cg_blks(fs, cgp, cylno)                                         \
        ((disk32(cgp->cg_magic) != CG_MAGIC)                            \
        ? (((struct ocg *)(cgp))->cg_b[cylno])                          \
        : ((short *)((char *)(cgp) + disk32(cgp->cg_boff)) +            \
          (cylno) * disk32(fs->fs_nrpos)))
#define cg_blktot(cgp)                                                  \
        ((disk32(cgp->cg_magic != CG_MAGIC))                            \
        ? (((struct ocg *)(cgp))->cg_btot)                              \
        : ((long *)((char *)(cgp) + disk32(cgp->cg_btotoff))))


#define NBBY 8
#define isset(a,i)      ((a)[(i)/NBBY] & (1<<((i)%NBBY)))
#define isclr(a,i)      (((a)[(i)/NBBY] & (1<<((i)%NBBY))) == 0)
