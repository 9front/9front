int	attachdev(Hub*, Port*);
void	detachdev(Hub*, Port*);
void	work(void);
Hub*	newhub(char *, Dev*, Hub*);
void	hname(char *);
void	checkidle(void);
