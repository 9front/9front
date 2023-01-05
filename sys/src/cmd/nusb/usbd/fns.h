int	attachdev(Port*);
void	detachdev(Port*);
void	work(void);
Hub*	newhub(char *, Dev*);
int	hname(char *);
void	assignhname(Dev *dev);
void	checkidle(void);
int	portfeature(Hub*, int, int, int);
