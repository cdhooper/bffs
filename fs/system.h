typedef struct {
        ULONG   fa_type;
        ULONG   fa_mode;
        ULONG   fa_nlink;
        ULONG   fa_uid;
        ULONG   fa_gid;
        ULONG   fa_size;
        ULONG   fa_blocksize;
        ULONG   fa_rdev;
        ULONG   fa_blocks;
        ULONG   fa_fsid;
        ULONG   fa_fileid;
        ULONG   fa_atime;
        ULONG   fa_atime_us;
        ULONG   fa_mtime;
        ULONG   fa_mtime_us;
        ULONG   fa_ctime;
        ULONG   fa_ctime_us;
} fileattr_t;

extern	char	*volumename;

struct	BFFSLock *CreateLock();
void	NewVolNode();
void	RemoveVolNode();
int	ResolveColon();
int	print_stats();
int	print_hash();
void	FillInfoBlock();
void    FillAttrBlock(fileattr_t *attr, struct icommon *finode);
int	open_filesystem(void);
