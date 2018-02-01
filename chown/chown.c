/* chown
 *      This program is copyright (1994-2018) Chris Hooper.  All code herein
 *      is freeware.  No portion of this code may be sold for profit.
 */

const char *version = "\0$VER: chown 1.2 (19-Jan-2018) © Chris Hooper";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>

#ifdef AMIGA
#include <dos/dos.h>
#include </dos30/dosextens.h>
#include <exec/memory.h>
#include <clib/exec_protos.h>
#include <clib/alib_protos.h>
#include <clib/dos_protos.h>
#define CTOB(x) ((x)>>2)

#else /* !AMIGA */
#include <unistd.h>
#endif

#define ulong unsigned long
#define ARRAY_SIZE(x) ((sizeof (x) / sizeof ((x)[0])))

static void
print_usage(const char *progname)
{
	fprintf(stderr,
		"%s\n"
		"usage: %s <owner>[.<group>] <filename...>\n"
		"       owner is an integer from 0 to 65535\n"
		"       group is an integer from 0 to 65535\n"
		"       filename is the file to change\n",
		version + 7, progname);
	exit(1);
}

#ifdef AMIGA
static int
chown(const char *name, ulong owner, ulong group)
{
    int                     err = 0;
    struct MsgPort          *msgport;
    struct MsgPort          *replyport;
    struct StandardPacket   *packet;
    BPTR                    lock;
    char                    buf[512];
    ulong                   ownergroup = (owner << 16) | group;

    msgport = (struct MsgPort *) DeviceProc(name);
    if (msgport == NULL)
	    return(1);

    lock = CurrentDir(0);
    CurrentDir(lock);

    replyport = (struct MsgPort *) CreatePort(NULL, 0);
    if (!replyport) {
	fprintf(stderr, "Unable to create reply port\n");
	return(1);
    }

    packet = (struct StandardPacket *)
	     AllocMem(sizeof(struct StandardPacket), MEMF_CLEAR | MEMF_PUBLIC);

    if (packet == NULL) {
	fprintf(stderr, "Unable to allocate memory\n");
	DeletePort(replyport);
	return(1);
    }

    strcpy(buf + 1, name);
    buf[0] = strlen(buf + 1);

    packet->sp_Msg.mn_Node.ln_Name = (char *) &(packet->sp_Pkt);
    packet->sp_Pkt.dp_Link         = &(packet->sp_Msg);
    packet->sp_Pkt.dp_Port         = replyport;
    packet->sp_Pkt.dp_Type         = ACTION_SET_OWNER;
    packet->sp_Pkt.dp_Arg1         = (LONG) NULL;
    packet->sp_Pkt.dp_Arg2         = lock;
    packet->sp_Pkt.dp_Arg3         = CTOB(buf);
    packet->sp_Pkt.dp_Arg4         = ownergroup;

    PutMsg(msgport, (struct Message *) packet);

    WaitPort(replyport);
    GetMsg(replyport);

    if (packet->sp_Pkt.dp_Res1 == DOSFALSE) {
	err = 1;
	fprintf(stderr, "error %d : ", packet->sp_Pkt.dp_Res2);
    }

    FreeMem(packet, sizeof(struct StandardPacket));
    DeletePort(replyport);

    return(err);
}

typedef struct pnode {
    ulong         id;
    char         *name;
    struct pnode *next;
} pnode_t;

static pnode_t *passwd_head = NULL;
static pnode_t *group_head = NULL;

static void
read_passwd_group(pnode_t **head, const char *const *files, int count)
{
    int             cur;
    APTR            save_ptr;
    struct Process *pr;

    /* Prevent requesters from popping up */
    pr = (struct Process *)FindTask(NULL);
    save_ptr = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)~0;
    for (cur = 0; cur < count; cur++) {
	FILE *fp = fopen(files[cur], "r");
	if (fp != NULL) {
	    char buf[80];
	    char name[16];
	    ulong id;
	    while (fgets(buf, sizeof (buf), fp) != NULL) {
		if ((strchr(buf, '|') &&
		     sscanf(buf, "%[^|]\|%[^|]\|%u", name, buf, &id) == 3) ||
		    (strchr(buf, ':') &&
		     sscanf(buf, "%[^:]:%u", name, &id) == 2)) {
		    pnode_t *temp = malloc(sizeof (*temp));
		    temp->name = strdup(name);
		    temp->id = id;
		    temp->next = *head;
		    *head = temp;
		}
	    }
	    fclose(fp);
	}
    }
    pr->pr_WindowPtr = save_ptr;  /* Restore requesters */
}

static void
read_passwd_and_group_files(void)
{
    const char * const pfiles[] = {
	"S:passwd",
	"DEVS:passwd",
	"INET:db/passwd",
	"ENV:passwd",
    };
    const char * const gfiles[] = {
	"S:group",
	"DEVS:group",
	"INET:db/group",
	"ENV:group",
    };

    read_passwd_group(&passwd_head, pfiles, ARRAY_SIZE(pfiles));
    read_passwd_group(&group_head, gfiles, ARRAY_SIZE(gfiles));
}

static void
free_passwd_and_group_files(void)
{
    pnode_t *temp;
    while (passwd_head != NULL) {
	temp = passwd_head;
	passwd_head = passwd_head->next;
	free(temp);
    }
    while (group_head != NULL) {
	temp = group_head;
	group_head = group_head->next;
	free(temp);
    }
}

static ulong
passwd_group_lookup(pnode_t *head, const char *name)
{
    int      pos = 0;
    ulong    val;
    pnode_t *temp;

    for (temp = head; temp != NULL; temp = temp->next)
	if (stricmp(temp->name, name) == 0)
	    return (temp->id);

    if ((sscanf(name, "%i%n", &val, &pos) == 1) &&
	(name[pos] == '\0')) {
	return (val);
    }
    return (-1);
}

static ulong
uid_lookup(const char *name)
{
    return (passwd_group_lookup(passwd_head, name));
}

static ulong
gid_lookup(const char *name)
{
    return (passwd_group_lookup(group_head, name));
}
#endif

int
main(int argc, char *argv[])
{
    int   index;
    ulong owner;
    ulong group;
    char  owner_str[64];
    char  group_str[64];
    int	  pos = 0;

    if (argc < 3)
	print_usage(argv[0]);

    group_str[0] = '\0';
    if (sscanf(argv[1], "%[^.]%n.%s%n", &owner_str, &pos,
	       &group_str, &pos) < 1) {
	fprintf(stderr, "Invalid <owner>[.<group>] in %s\n", argv[1]);
	exit(1);
    }
    if (argv[1][pos] != '\0') {
	fprintf(stderr, "Invalid <owner>.<group> in %s\n", argv[1]);
	exit(1);
    }

#ifdef AMIGA
    read_passwd_and_group_files();

    owner = uid_lookup(owner_str);
    if (owner == -1) {
	fprintf(stderr, "Unknown user %s\n", owner_str);
	exit(1);
    }
    if (group_str[0] == '\0') {
	group = 65535;
    } else {
	group = gid_lookup(group_str);
	if (group == -1) {
	    fprintf(stderr, "Unknown group %s\n", group_str);
	    exit(1);
	}
    }
#endif

    if (owner > 65535) {
	fprintf(stderr, "Invalid owner in %s\n\n", argv[1]);
	print_usage(argv[0]);
    }

    if (group > 65535) {
	fprintf(stderr, "Invalid group in %s\n\n", argv[1]);
	print_usage(argv[0]);
    }

    for (index = 2; index < argc; index++)
	if (chown(argv[index], owner, group))
	    fprintf(stderr, "Unable to set owner/group of %s\n", argv[index]);

#ifdef AMIGA
    free_passwd_and_group_files();
#endif

    exit(0);
}
