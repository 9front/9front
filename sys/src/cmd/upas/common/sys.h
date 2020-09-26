#include <u.h>
#include <libc.h>
#include <bio.h>

/*
 *  for the lock routines in libsys.c
 */
typedef struct Mlock	Mlock;
struct Mlock {
	int	fd;
	int	pid;
	char	name[Pathlen];
};

/*
 *  from config.c
 */
extern char *MAILROOT;	/* root of mail system */
extern char *SPOOL;	/* spool directory; for spam ctl */
extern char *UPASLOG;	/* log directory */
extern char *UPASLIB;	/* upas library directory */
extern char *UPASBIN;	/* upas binary directory */
extern char *UPASTMP;	/* temporary directory */
extern char *SHELL;	/* path name of shell */

enum {
	Mboxmode	= 0622,
};

/*
 *  files in libsys.c
 */
char	*sysname_read(void);
char	*alt_sysname_read(void);
char	*domainname_read(void);
char	**sysnames_read(void);
char	*getlog(void);
Tmfmt	thedate(Tm*);
Biobuf	*sysopen(char*, char*, ulong);
int	sysopentty(void);
int	sysclose(Biobuf*);
int	sysmkdir(char*, ulong);
Mlock	*syslock(char *);
void	sysunlock(Mlock *);
void	syslockrefresh(Mlock *);
int	sysrename(char*, char*);
int	sysexist(char*);
int	syskill(int);
int	syskillpg(int);
Mlock	*trylock(char *);
void	pipesig(int*);
void	pipesigoff(void);
int	holdon(void);
void	holdoff(int);
int	syscreatelocked(char*, int, int);
int	sysopenlocked(char*, int);
int	sysunlockfile(int);
int	sysfiles(void);
int 	become(char**, char*);
int	sysdetach(void);
char	*username(char*);
int	creatembox(char*, char*);
int	createfolder(char*, char*);
