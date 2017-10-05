#define SunBB_MAGIC	0xDABE		/* Sun Boot block magic number		*/

/* This structure was also created purely from examination of a Sun boot sector.
   I guarantee nothing. */
struct sun_label {
	char	bb_mfg_label[416];	/* Written by label cmd in format	*/
	u_long	bb_reserved1;		/* Dunno				*/
	u_short bb_rpm; 		/* Disk rotational speed per minute	*/
	u_short bb_ncyl;		/* Total disk cylinders			*/
	u_long	bb_reserved2;		/* Dunno				*/
	u_long	bb_hw_interleave;	/* Hardware sector interleave		*/
	u_short bb_nfscyl;		/* Cylinders usable for filesystems	*/
	u_short bb_ncylres;		/* Number of reserved cylinders		*/
	u_short bb_heads;		/* Number of heads per cylinder		*/
	u_short bb_nspt;		/* Number of sectors per track		*/
	u_long	bb_reserved3;		/* Dunno				*/
	struct	sun_part_info {
		u_long fs_start_cyl;	/* Offset in disk cyl for filesystem	*/
		u_long fs_size;		/* Size in hw disk blocks		*/
	} bb_part[MAX_PART];
	u_short bb_magic;		/* magic number - 0xDABE		*/
	u_short bb_csum;		/* checksum				*/
};
