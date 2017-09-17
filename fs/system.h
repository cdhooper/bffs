extern	char	*volumename;

struct	BFFSLock *CreateLock();
void	NewVolNode();
void	RemoveVolNode();
int	ResolveColon();
int	print_stats();
int	print_hash();
void	FillInfoBlock();
