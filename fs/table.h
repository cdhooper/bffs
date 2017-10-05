/* store debug packet info if compiled with debug option */
#ifdef DEBUG
#	define I(x) x
#else
#	define I(x) /* x */
#endif

#define LAST_PACKET	0

/* check for inhibit? */
#define NOCHK		0
#define CHECK		1

struct call_table {
	long	packet_type;
	void	(*routine)();
	int	check_inhibit;
#ifdef DEBUG
	char	*name;
#endif
};

struct direct_table {
	void	(*routine)();
	int	check_inhibit;
#ifdef DEBUG
	char	*name;
#endif
};

extern struct call_table   packet_table[];
extern struct direct_table dpacket_table[];
