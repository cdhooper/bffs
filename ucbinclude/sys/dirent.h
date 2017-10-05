struct  dirent {
        off_t           d_off;
        unsigned long   d_fileno;
        unsigned short  d_reclen;
        unsigned short  d_namlen;
        char            d_name[255+1];
};

#define MAXNAMLEN 255

#ifndef DIRSIZ
#define DIRSIZ(dp) (((sizeof(struct dirent) - (MAXNAMLEN+1) + ((dp)->d_namlen+1)) +3) & ~3)
#endif
