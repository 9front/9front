#pragma lib "./lib.$O.a"

enum{
	MAXNETCHAL	= 100000,		/* max securenet challenge */
	Maxpath		= 256,
};

#define	KEYDB		"/mnt/keys"
#define NETKEYDB	"/mnt/netkeys"
#define KEYDBBUF	(sizeof NETKEYDB)	/* enough for any keydb prefix */
#define AUTHLOG		"auth"

enum
{
	Nemail		= 10,
	Plan9		= 1,
	Securenet	= 2,
};

typedef struct
{
	char	*user;
	char	*postid;
	char	*name;
	char	*dept;
	char	*email[Nemail];
} Acctbio;

typedef struct {
	char	*keys;
	char	*msg;
	char	*who;
	Biobuf 	*b;
} Fs;

extern Fs fs[3];

int	answer(char*);
void	checksum(char*, char*);
void	error(char*, ...);
void	fail(char*);
int	findkey(char*, char*, Authkey*);
char*	finddeskey(char*, char*, char*);
uchar*	findaeskey(char*, char*, uchar*);
char*	findsecret(char*, char*, char*);
int	getauthkey(Authkey*);
long	getexpiration(char *db, char *u);
void	getpass(Authkey*, char*, int, int);
int	deskeyfmt(Fmt*);
void	logfail(char*);
int	netcheck(void*, long, char*);
char*	netdecimal(char*);
char*	netresp(char*, long, char*);
char*	okpasswd(char*);
void	private(void);
int	querybio(char*, char*, Acctbio*);
void	rdbio(char*, char*, Acctbio*);
int	readarg(int, char*, int);
int	readfile(char*, char*, int);
char*	secureidcheck(char*, char*);
int	setkey(char*, char*, Authkey*);
char*	setdeskey(char*, char*, char*);
uchar*	setaeskey(char*, char*, uchar*);
char*	setsecret(char*, char*, char*);
int	smartcheck(void*, long, char*);
void	succeed(char*);
void	wrbio(char*, Acctbio*);
int	writefile(char*, char*, int);

#pragma	varargck	type	"K"	char*
