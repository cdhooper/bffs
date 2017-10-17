#ifndef _TABLE_H

/* check for inhibit? */
#define NOCHK		0
#define CHECK		1

typedef void (*packet_func_t)(void);

packet_func_t packet_lookup(LONG ptype, const char **name, int *check_inhibit);

#endif /* _TABLE_H */
