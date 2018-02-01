#ifndef _BSD_LABEL_H
#define _BSD_LABEL_H

#define B44BB_MAGIC   0x82564557 /* BSD 44 Boot block magic number   */
#define B44BB_MAGSWAP 0x57455682 /* BSD 44 Boot block magic number   */

/*
 * This structure was created purely from examination of a BSD boot sector.
 * I guarantee nothing.
 */
struct bsd44_label {
    u_long  bb_magic;
    short   bb_dtype;
    short   bb_dsubtype;
    char    bb_mfg_label[16];
    char    bb_packname[16];

    u_long  bb_secsize;        /* Number of bytes per sector            */
    u_long  bb_nspt;           /* Number of sectors per track           */
    u_long  bb_heads;          /* Number of tracks per cylinder         */
    u_long  bb_ncyl;           /* Total disk cylinders                  */
    u_long  bb_nspc;           /* Number of sectors per cylinder        */
    u_long  bb_nsec;           /* Total disk sectors                    */

    u_short bb_spare_spt;      /* Spares per track                      */
    u_short bb_spare_spc;      /* Spares per cylinder                   */
    u_long  bb_spare_cyl;      /* Total spare cylinders                 */

    u_short bb_rpm;            /* Disk rotational speed per minute      */
    u_short bb_hw_interleave;  /* Hardware sector interleave            */
    u_short bb_trackskew;      /* Hardware track skew                   */
    u_short bb_cylskew;        /* Hardware cylinder skew                */
    u_long  bb_headswitch;     /* Hardware head switch time (µsec)      */
    u_long  bb_trkseek;        /* Hardware track-track seek time (µsec) */
    u_long  bb_flags;          /* Hardware device flags                 */

    u_long  bb_drive_data[5];  /* drive data                            */
    u_long  bb_spares[5];      /* reserved spare info                   */
    u_long  bb_magic2;         /* boot block magic number               */
    u_short bb_checksum;       /* boot block checksum                   */

    u_short bb_npartitions;    /* number of partitions                  */
    u_long  bb_bbsize;         /* size of boot area                     */
    u_long  bb_sbsize;         /* size of superblock                    */
    struct  bsd44_part_info {
        u_long  fs_size;       /* Size in hw disk blocks                */
        u_long  fs_start_sec;  /* Partition start offset in sectors     */
        u_long  fs_fsize;      /* Filesystem fragment size              */
        u_char  fs_type;       /* Filesystem type                       */
        u_char  fs_frag;       /* Filesystem frags per block            */
        u_short fs_cpg;        /* Filesystem cylinders per group        */
    } bb_part[MAX_PART];
};

#endif /* _BSD_LABEL_H */
