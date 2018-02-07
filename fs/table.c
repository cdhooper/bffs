/*
 * Copyright 2018 Chris Hooper <amiga@cdh.eebugs.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted so long as any redistribution retains the
 * above copyright notice, this condition, and the below disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <dos/dosextens.h>
#include "config.h"
#include "table.h"
#include "packets.h"
#include "handler.h"
#include "bffs_dosextens.h"

/* store debug packet info if compiled with debug option */
#ifdef DEBUG
#define I(x) , x
#else
#define I(x) /* x */
#endif

typedef struct {
    void    (*routine)();
    char    check_inhibit;
#ifdef DEBUG
    char    name[15];
#endif
} direct_table_t;

typedef struct {
    long    packet_type;
    void    (*routine)();
    char    check_inhibit;
#ifdef DEBUG
    char    name[15];
#endif
} search_table_t;

static const direct_table_t dpacket_table[] = {
{ PNil,           CHECK I("NIL")            },   /* 0 */
{ PUnimplemented, NOCHK I("Unknown 1")      },   /* 1 */
{ PUnimplemented, NOCHK I("U GET_BLOCK")    },   /* 2 */
{ PUnimplemented, NOCHK I("Unknown 3")      },   /* 3 */
{ PUnimplemented, NOCHK I("U SET_MAP")      },   /* 4 */
{ PDie,           NOCHK I("DIE")            },   /* 5 */
{ PUnimplemented, NOCHK I("U EVENT")        },   /* 6 */
{ PCurrentVolume, CHECK I("CURRENT_VOLUME") },   /* 7 */
{ PLocateObject,  CHECK I("LOCATE_OBJECT")  },   /* 8 */
{ PRenameDisk,    CHECK I("RENAME_DISK")    },   /* 9 */
{ PUnimplemented, NOCHK I("Unknown 10")     },   /* 10 */
{ PUnimplemented, NOCHK I("Unknown 11")     },   /* 11 */
{ PUnimplemented, NOCHK I("Unknown 12")     },   /* 12 */
{ PUnimplemented, NOCHK I("Unknown 13")     },   /* 13 */
{ PUnimplemented, NOCHK I("Unknown 14")     },   /* 14 */
{ PFreeLock,      CHECK I("FREE_LOCK")      },   /* 15 */
{ PDeleteObject,  CHECK I("DELETE_OBJECT")  },   /* 16 */
{ PRenameObject,  CHECK I("RENAME_OBJECT")  },   /* 17 */
{ PMoreCache,     CHECK I("MORE_CACHE")     },   /* 18 */
{ PCopyDir,       CHECK I("COPY_DIR")       },   /* 19 */
{ PUnimplemented, NOCHK I("U WAIT_CHAR")    },   /* 20 */
{ PSetProtect,    CHECK I("SET_PROTECT")    },   /* 21 */
{ PCreateDir,     CHECK I("CREATE_DIR")     },   /* 22 */
{ PExamineObject, CHECK I("EXAMINE_OBJECT") },   /* 23 */
{ PExamineNext,   CHECK I("EXAMINE_NEXT")   },   /* 24 */
{ PDeviceInfo,    CHECK I("DISK_INFO")      },   /* 25 */
{ PDeviceInfo,    CHECK I("INFO")           },   /* 26 */
{ PFlush,         CHECK I("FLUSH")          },   /* 27 */
{ PUnimplemented, NOCHK I("U SET_COMMENT")  },   /* 28 */
{ PParent,        CHECK I("PARENT")         },   /* 29 */
{ PFlush,         CHECK I("TIMER")          },   /* 30 */
{ PInhibit,       NOCHK I("INHIBIT")        },   /* 31 */
{ PUnimplemented, CHECK I("U DISK_TYPE")    },   /* 32 */
{ PDiskChange,    CHECK I("DISK_CHANGE")    },   /* 33 */
{ PSetDate,       CHECK I("SET_DATE")       },   /* 34 */
{ PUnimplemented, NOCHK I("Unknown 35")     },   /* 35 */
{ PUnimplemented, NOCHK I("Unknown 36")     },   /* 36 */
{ PUnimplemented, NOCHK I("Unknown 37")     },   /* 37 */
{ PUnimplemented, NOCHK I("Unknown 38")     },   /* 38 */
{ PUnimplemented, NOCHK I("Unknown 39")     },   /* 39 */
{ PSameLock,      CHECK I("SAME_LOCK")      },   /* 40 */
};

static const search_table_t spacket_table[] = {
{ ACTION_READ,           PRead,          CHECK I("READ")           },
{ ACTION_WRITE,          PWrite,         CHECK I("WRITE")          },
{ ACTION_FINDINPUT,      PFindInput,     CHECK I("FINDINPUT")      },
{ ACTION_FINDOUTPUT,     PFindOutput,    CHECK I("FINDOUTPUT")     },
{ ACTION_END,            PEnd,           CHECK I("END")            },

{ ACTION_EX_OBJECT,      PExamineObject, CHECK I("EX_OBJECT")      },
{ ACTION_EX_NEXT,        PExamineNext,   CHECK I("EX_NEXT")        },

{ ACTION_CREATE_OBJECT,  PCreateObject,  CHECK I("CREATE_OBJECT")  },
{ ACTION_MAKE_LINK,      PMakeLink,      CHECK I("MAKE_LINK")      },
{ ACTION_SET_OWNER,      PSetOwner,      CHECK I("SET_OWNER")      },
{ ACTION_IS_FILESYSTEM,  PIsFilesystem,  CHECK I("IS_FILESYSTEM")  },
{ ACTION_SEEK,           PSeek,          CHECK I("SEEK")           },
{ ACTION_FINDUPDATE,     PFindInput,     CHECK I("FINDUPDATE")     },
{ ACTION_READ_LINK,      PReadLink,      CHECK I("READ_LINK")      },
{ ACTION_SET_FILE_SIZE,  PSetFileSize,   CHECK I("SET_FILE_SIZE")  },
{ ACTION_SET_DATES,      PSetDates,      CHECK I("SET_DATES")      },
{ ACTION_SET_TIMES,      PSetTimes,      CHECK I("SET_TIMES")      },
{ ACTION_GET_PERMS,      PGetPerms,      CHECK I("GET_PERMS")      },
{ ACTION_SET_PERMS,      PSetPerms,      CHECK I("SET_PERMS")      },

{ ACTION_EXAMINE_FH,     PExamineFh,     CHECK I("EXAMINE_FH")     },
{ ACTION_GET_DISK_FSSM,  PGetDiskFSSM,   NOCHK I("GET_DISK_FSSM")  },
{ ACTION_FREE_DISK_FSSM, PFreeDiskFSSM,  NOCHK I("FREE_DISK_FSSM") },

{ ACTION_FS_STATS,       PFilesysStats,  NOCHK I("FS_STATS")       },
{ ACTION_WRITE_PROTECT,  PWriteProtect,  CHECK I("WRITE_PROTECT")  },
{ ACTION_DISK_CHANGE,    PDiskChange,    CHECK I("DISK_CHANGE")    },
{ ACTION_FORMAT,         PFormat,        CHECK I("FORMAT")         },

#ifdef NOT_IMPLEMENTED
/* commented out */
{ ACTION_CHANGE_MODE,    PUnimplemented, NOCHK I("CHANGE_MODE")    },
{ ACTION_LOCK_RECORD,    PUnimplemented, NOCHK I("LOCK_RECORD")    },
{ ACTION_FREE_RECORD,    PUnimplemented, NOCHK I("FREE_RECORD")    },
{ ACTION_SCREEN_MODE,    PUnimplemented, NOCHK I("SCREEN_MODE")    },
{ ACTION_READ_RETURN,    PUnimplemented, NOCHK I("READ_RETURN")    },
{ ACTION_WRITE_RETURN,   PUnimplemented, NOCHK I("WRITE_RETURN")   },

{ ACTION_FH_FROM_LOCK,   PFhFromLock,    CHECK I("FH_FROM_LOCK")   },
{ ACTION_COPY_DIR_FH,    PCopyDirFh,     CHECK I("COPY_DIR_FH")    },
{ ACTION_PARENT_FH,      PParentFh,      CHECK I("PARENT_FH")      },
{ ACTION_EXAMINE_ALL,    PExamineAll,    CHECK I("EXAMINE_ALL")    },
{ ACTION_ADD_NOTIFY,     PAddNotify,     CHECK I("ADD_NOTIFY")     },
{ ACTION_REMOVE_NOTIFY,  PRemoveNotify,  CHECK I("REMOVE_NOTIFY")  },
#endif  /* NOT_IMPLEMENTED */
};


#ifdef JUST_FOR_DOC
/*
 * The below packets are implemented in the direct table
 */
{ ACTION_NIL,            PNil,           NOCHK I("NIL")            },
{ ACTION_GET_BLOCK,      PUnimplemented, NOCHK I("GET_BLOCK")      },
{ ACTION_SET_MAP,        PUnimplemented, NOCHK I("SET_MAP")        },
{ ACTION_DIE,            PDie,           NOCHK I("DIE")            },
{ ACTION_EVENT,          PUnimplemented, NOCHK I("EVENT")          },
{ ACTION_CURRENT_VOLUME, PCurrentVolume, CHECK I("CURRENT_VOLUME") },
{ ACTION_LOCATE_OBJECT,  PLocateObject,  CHECK I("LOCATE_OBJECT")  },
{ ACTION_RENAME_DISK,    PRenameDisk,    CHECK I("RENAME_DISK")    },
{ ACTION_FREE_LOCK,      PFreeLock,      CHECK I("FREE_LOCK")      },
{ ACTION_DELETE_OBJECT,  PDeleteObject,  CHECK I("DELETE_OBJECT")  },
{ ACTION_RENAME_OBJECT,  PRenameObject,  CHECK I("RENAME_OBJECT")  },
{ ACTION_MORE_CACHE,     PMoreCache,     CHECK I("MORE_CACHE")     },
{ ACTION_COPY_DIR,       PCopyDir,       CHECK I("COPY_DIR")       },
{ ACTION_WAIT_CHAR,      PUnimplemented, NOCHK I("WAIT_CHAR")      },
{ ACTION_SET_PROTECT,    PSetProtect,    CHECK I("SET_PROTECT")    },
{ ACTION_CREATE_DIR,     PCreateDir,     CHECK I("CREATE_DIR")     },
{ ACTION_EXAMINE_OBJECT, PExamineObject, CHECK I("EXAMINE_OBJECT") },
{ ACTION_EXAMINE_NEXT,   PExamineNext,   CHECK I("EXAMINE_NEXT")   },
{ ACTION_DISK_INFO,      PDeviceInfo,    CHECK I("DISK_INFO")      },
{ ACTION_INFO,           PDeviceInfo,    CHECK I("INFO")           },
{ ACTION_FLUSH,          PFlush,         CHECK I("FLUSH")          },
{ ACTION_SET_COMMENT,    PUnimplemented, NOCHK I("SET_COMMENT")    },
{ ACTION_PARENT,         PParent,        CHECK I("PARENT")         },
{ ACTION_TIMER,          PFlush,         CHECK I("TIMER")          },
{ ACTION_INHIBIT,        PInhibit,       NOCHK I("INHIBIT")        },
{ ACTION_DISK_TYPE,      PUnimplemented, NOCHK I("DISK_TYPE")      },
{ ACTION_SET_DATE,       PSetDate,       CHECK I("SET_DATE")       },
{ ACTION_SAME_LOCK,      PSameLock,      CHECK I("SAME_LOCK")      },
#endif  /* JUST_FOR_DOC */


packet_func_t
packet_lookup(LONG ptype, const char **name, int *check_inhibit)
{
    search_table_t *stable;
    int             index;

    if ((ptype < ARRAY_SIZE(dpacket_table)) && (ptype >= 0)) {
        /* Direct packet table */
        direct_table_t *dtable = &dpacket_table[ptype];

#ifdef DEBUG
        *name = dtable->name;
#endif
        *check_inhibit = dtable->check_inhibit;
        return (dtable->routine);
    }

    stable = &spacket_table[0];
    for (index = ARRAY_SIZE(spacket_table); index > 0; index--, stable++) {
        if (stable->packet_type == ptype) {
#ifdef DEBUG
            *name = stable->name;
#endif
            *check_inhibit = stable->check_inhibit;
            return (stable->routine);
        }
    }

    return (NULL);
}
