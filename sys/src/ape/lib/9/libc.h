#define _LOCK_EXTENSION
#define _QLOCK_EXTENSION
#define _BSD_EXTENSION

#ifdef _NET_EXTENSION
#include <libnet.h>
#endif

#include <stdint.h>
#include <sys/types.h>
#include <lock.h>
#include <qlock.h>
#include <lib9.h>
#include <stdlib.h>
#include <string.h>
#include <bsd.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <utf.h>
#include <fmt.h>
#include <signal.h>
#include <time.h>

#define	nelem(x)	(sizeof(x)/sizeof((x)[0]))

typedef
struct Qid
{
	uvlong	path;
	ulong	vers;
	uchar	type;
} Qid;

typedef
struct Dir {
	/* system-modified data */
	ushort	type;	/* server type */
	uint	dev;	/* server subtype */
	/* file data */
	Qid	qid;	/* unique id from server */
	ulong	mode;	/* permissions */
	ulong	atime;	/* last read time */
	ulong	mtime;	/* last write time */
	vlong	length;	/* file length: see <u.h> */
	char	*name;	/* last element of path */
	char	*uid;	/* owner name */
	char	*gid;	/* group name */
	char	*muid;	/* last modifier name */
} Dir;

uint	_convM2D(uchar*, uint, Dir*, char*);
uint	_convD2M(Dir*, uchar*, uint);
Dir	*_dirstat(char*);
int	_dirwstat(char*, Dir*);
Dir	*_dirfstat(int);
int	_dirfwstat(int, Dir*);
long	_dirread(int, Dir**);
long _dirreadall(int, Dir**);
void _nulldir(Dir*);
uint _sizeD2M(Dir*);

#define convM2D	_convM2D
#define convD2M	_convD2M
#define dirstat _dirstat
#define dirwstat _dirwstat
#define dirfstat _dirfstat
#define dirfwstat _dirfwstat
#define dirread _dirread
#define dirreadall _dirreadall
#define nulldir _nulldir
#define sizeD2M _sizeD2M

typedef
struct Waitmsg
{
	int pid;	/* of loved one */
	unsigned long time[3];	/* of loved one & descendants */
	char	*msg;
} Waitmsg;

extern	int	_AWAIT(char*, int);
extern	int	_ALARM(unsigned long);
extern	int	_BIND(const char*, const char*, int);
extern	int	_CHDIR(const char*);
extern	int	_CLOSE(int);
extern	int	_CREATE(char*, int, unsigned long);
extern	int	_DUP(int, int);
extern	int	_ERRSTR(char*, unsigned int);
extern	int	_EXEC(char*, char*[]);
extern	void	_EXITS(char *);
extern	int	_FD2PATH(int, char*, int);
extern	int	_FAUTH(int, char*);
extern	int	_FSESSION(int, char*, int);
extern	int	_FSTAT(int, unsigned char*, int);
extern	int	_FWSTAT(int, unsigned char*, int);
extern	int	_MOUNT(int, int, const char*, int, const char*);
extern	int	_NOTED(int);
extern	int	_NOTIFY(int(*)(void*, char*));
extern	int	_OPEN(const char*, int);
extern	int	_PIPE(int*);
extern	long	_PREAD(int, void*, long, long long);
extern	long	_PWRITE(int, void*, long, long long);
extern	long	_READ(int, void*, long);
extern	int	_REMOVE(const char*);
extern	void*	_RENDEZVOUS(void*, void*);
extern	int	_RFORK(int);
extern	void*	_SEGATTACH(int, char*, void*, unsigned long);
extern	void*	_SEGBRK(void*, void*);
extern	int	_SEGDETACH(void*);
extern	int	_SEGFLUSH(void*, unsigned long);
extern	int	_SEGFREE(void*, unsigned long);
extern	long long	_SEEK(int, long long, int);
extern	int	_SLEEP(long);
extern	int	_STAT(const char*, unsigned char*, int);
extern	Waitmsg*	_WAIT(void);
extern	long	_WRITE(int, const void*, long);
extern	int	_WSTAT(const char*, unsigned char*, int);
extern 	void*	_MALLOCZ(int, int);
extern	int	_WERRSTR(char*, ...);
extern	long	_READN(int, void*, long);
extern	int	_IOUNIT(int);
extern	vlong	_NSEC(void);

#define OREAD 0
#define OWRITE 1
#define ORDWR 2
#define	OEXEC	3	/* execute, == read but check execute permission */
#define	OTRUNC	16	/* or'ed in (except for exec), truncate file first */
#define	OCEXEC	32	/* or'ed in, close on exec */
#define	ORCLOSE	64	/* or'ed in, remove on close */
#define	OEXCL	0x1000	/* or'ed in, exclusive use (create only) */

#define AREAD 4
#define AWRITE 2
#define AEXEC 1
#define AEXIST 0

#define _exits(s) _exit(s && *(char*)s ? 1 : 0)
#define exits(s) exit(s && *(char*)s ? 1 : 0)

#define create(file, omode, perm) open(file, (omode) |O_CREAT | O_TRUNC, perm)
#define seek(fd, off, dir) lseek(fd, off, dir)

#define fauth _FAUTH
#define wait _WAIT
#define readn _READN
#define pread _PREAD
#define pwrite _PWRITE
#define mallocz _MALLOCZ
#define nsec	_NSEC
#define iounit	_IOUNIT
#define sleep	_SLEEP

#define getwd(buf,len)	getcwd(buf,len)
#define postnote(who,pid,note)	kill(pid,SIGTERM)
#define atnotify(func,in)

#define ERRMAX 128

int errstr(char*, unsigned int);
void rerrstr(char*, unsigned int);
extern _Noreturn void sysfatal(char*, ...);

extern	void		setmalloctag(void*, uintptr_t);
extern	void		setrealloctag(void*, uintptr_t);
extern	uintptr_t	getcallerpc(void*);

extern int  dec16(uchar *, int, char *, int);
extern int  enc16(char *, int, uchar *, int);
extern int  dec32(uchar *, int, char *, int);
extern int  enc32(char *, int, uchar *, int);
extern int  dec64(uchar *, int, char *, int);
extern int  enc64(char *, int, uchar *, int);

extern	int	dec64chr(int);
extern	int	enc64chr(int);
extern	int	dec32chr(int);
extern	int	enc32chr(int);
extern	int	dec16chr(int);
extern	int	enc16chr(int);

extern	int	idn2utf(char *, char *, int);
extern	int	utf2idn(char *, char *, int);

extern int tokenize(char*, char**, int);
extern int getfields(char*, char**, int, int, char*);
extern int gettokens(char*, char**, int, char*);

extern ulong truerand(void); /* uses /dev/random */

extern int encrypt(void*, void*, int len);
extern int decrypt(void*, void*, int len);

typedef
struct Tm
{
	int	sec;
	int	min;
	int	hour;
	int	mday;
	int	mon;
	int	year;
	int	wday;
	int	yday;
	char	zone[4];
	int	tzoff;
} Tm;

Tm* _gmtime(time_t);
#define gmtime _gmtime
