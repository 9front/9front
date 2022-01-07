void	Abort(void);
int	Chdir(char*);
void	Close(int);
void	Closedir(void*);
int	Creat(char*);
int	Dup(int, int);
int	Dup1(int);
int	Eintr(void);
int	Executable(char*);
void	Exec(char**);
void	Exit(void);
char*	Errstr(void);
char*	Freeword(word*);
int	Fork(void);
char*	Getstatus(void);
int	Isatty(int);
word*	Newword(char*,word*);
void	Noerror(void);
int	Open(char*, int);
void*	Opendir(char*);
word*	Poplist(void);
char*	Popword(void);
word*	Pushword(char*);
long	Read(int, void*, long);
char*	Readdir(void*, int);
long	Seek(int, long, long);
void	Setstatus(char*);
void	Trapinit(void);
void	Updenv(void);
void	Vinit(void);
int	Waitfor(int);
long	Write(int, void*, long);
void	addwaitpid(int);
void	clearwaitpids(void);
void	codefree(code*);
int	compile(tree*);
int	count(word*);
char*	deglob(char*);
void	delwaitpid(int);
void	dotrap(void);
void	freenodes(void);
void	freewords(word*);
void	globword(word*);
int	havewaitpid(int);
int	idchr(int);
void	inttoascii(char*, int);
void	kinit(void);
int	mapfd(int);
int	match(char*, char*, int);
char*	makepath(char*, char*);
void	panic(char*, int);
void	pfln(io*, char*, int);
void	poplist(void);
void	popword(void);
void	pprompt(void);
void	Prompt(char*);
void	psubst(io*, unsigned char*);
void	pushlist(void);
void	pushredir(int, int, int);
word*	pushword(char*);
void	readhere(io*);
void	heredoc(tree*);
void	setstatus(char*);
void	skipnl(void);
void	start(code*, int, var*, redir*);
int	truestatus(void);
void	usage(char*);
int	wordchr(int);
void	yyerror(char*);
int	yylex(void);
int	yyparse(void);
