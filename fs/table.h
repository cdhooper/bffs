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

struct direct_table {
	void	(*routine)();
	int	check_inhibit;
#ifdef DEBUG
	char	*name;
#endif
};

struct search_table {
	long	packet_type;
	void	(*routine)();
	int	check_inhibit;
#ifdef DEBUG
	char	*name;
#endif
};

extern struct direct_table dpacket_table[];
extern struct search_table spacket_table[];
