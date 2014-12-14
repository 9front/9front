enum {
	Debug = 0,
};

extern void	fatal(char*);
extern int	readfile(char*, char*, int);
extern void	run(char*, ...);
extern void	setenv(char*, char*, int);
extern int	writefile(char*, char*, int);
