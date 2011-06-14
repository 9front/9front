enum {
	Debug = 0,
};

extern char*	bootdisk;
extern char*	rootdir;
extern int		(*cfs)(int);
extern int		cpuflag;

extern void fatal(char*);
extern int	readfile(char*, char*, int);
extern void run(char*, ...);
extern void	setenv(char*, char*, int);
extern int	writefile(char*, char*, int);
extern void	boot(int, char **);
