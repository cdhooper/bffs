#define NO_CALL		0
#define Not_Implemented	1

#define NO_ARG	0
#define PASS	1
#define CHANGE	2

#define LAST_PACKET	-1

struct call_table {
	long	packet_type;
	void	(*routine)();
	int	check_inhibit;
	int	call_type;
	char	*name;
};

extern struct call_table packet_table[];

#define NON 0
#define NNN 1
#define NCN 2
#define CNN 6
#define CPN 7
#define CCN 8
#define CCP 9
#define CCC 10
#define ALL 11
#define BAD 12
