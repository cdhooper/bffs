struct fstab {
    char       *fs_spec;
    char       *fs_file;
    char       *fs_vfstype;
    char       *fs_mntops;
    const char *fs_type;
    int         fs_freq;
    int         fs_passno;
};

#define FSTAB_RO "ro"
#define FSTAB_RQ "rq"
#define FSTAB_RW "rw"

