/*
 * sorted by Edit 4,/^$/|sort -bd +1
 */
int	Bimapaddr(Biobuf*, Maddr*);
int	Bimapmimeparams(Biobuf*, Mimehdr*);
int	Bnlist(Biobuf*, Nlist*, char*);
int	Bslist(Biobuf*, Slist*, char*);
int	Dfmt(Fmt*);
int	δfmt(Fmt*);
int	Ffmt(Fmt*);
int	Xfmt(Fmt*);
int	Zfmt(Fmt*);
int	appendsave(char*, int , char*, Biobuf*, long, Uidplus*);
void	bye(char*, ...);
int	cdcreate(char*, char*, int, ulong);
Dir	*cddirstat(char*, char*);
int	cddirwstat(char*, char*, Dir*);
int	cdexists(char*, char*);
int	cdopen(char*, char*, int);
int	cdremove(char*, char*);
Mblock	*checkbox(Box*, int );
void	closebox(Box*, int opened);
void	closeimp(Box*, Mblock*);
int	copycheck(Box*, Msg*, int uids, void*);
int	copysaveu(Box*, Msg*, int uids, void*);
char	*cramauth(void);
char	*crauth(char*, char*);
int	creatembox(char*);
Tm	*date2tm(Tm*, char*);
void	debuglog(char*, ...);
char	*decfs(char*, int, char*);
char	*decmutf7(char*, int, char*);
int	deletemsg(Box *, Msgset*);
void	*emalloc(ulong);
int	emptyimp(char*);
void	enableforwarding(void);
char	*encfs(char*, int, char*);
char	*encmutf7(char*, int nout, char*);
void	*erealloc(void*, ulong);
char	*estrdup(char*);
int	expungemsgs(Box*, int);
void	*ezmalloc(ulong);
void	fetchbody(Msg*, Fetch*);
void	fetchbodyfill(uint);
Pair	fetchbodypart(Fetch*, uint);
void	fetchbodystr(Fetch*, char*, uint);
void	fetchbodystruct(Msg*, Header*, int);
void	fetchenvelope(Msg*);
int	fetchmsg(Box*, Msg *, int, void*);
Msg	*fetchsect(Msg*, Fetch*);
int	fetchseen(Box*, Msg*, int, void*);
void	fetchstructext(Header*);
Msg	*findmsgsect(Msg*, Nlist*);
int	formsgs(Box*, Msgset*, uint, int, int (*)(Box*, Msg*, int, void*), void*);
int	fqid(int, Qid*);
void	freemsg(Box*, Msg*);
vlong	getquota(void); 
void	ilog(char*, ...);
int	imap4date(Tm*, char*);
ulong	imap4datetime(char*);
int	imaptmp(void);
char	*impname(char*);
int	inmsgset(Msgset*, uint);
int	isdotdot(char*);
int	isprefix(char*, char*);
int	issuffix(char*, char*);
int	listboxes(char*, char*, char*);
char	*loginauth(char*, char*);
int	lsubboxes(char*, char*, char*);
char	*maddrstr(Maddr*);
uint	mapflag(char*);
uint	mapint(Namedint*, char*);
int	mblocked(void);
void	mblockrefresh(Mblock*);
Mblock	*mblock(void);
char	*mboxname(char*);
void	mbunlock(Mblock*);
Fetch	*mkfetch(int, Fetch*);
Slist	*mkslist(char*, Slist*);
Store	*mkstore(int, int, int);
int	movebox(char*, char*);
void	msgdead(Msg*);
int	msgfile(Msg*, char*);
int	msginfo(Msg*);
int	msgis822(Header*);
int	msgismulti(Header*);
int	msgseen(Box*, Msg*);
uint	msgsize(Msg*);
int	msgstruct(Msg*, int top);
char	*mutf7str(char*);
int	mychdir(char*);
int	okmbox(char*);
Box	*openbox(char*, char*, int);
int	openlocked(char*, char*, int);
void	parseerr(char*);
int	parseimp(Biobuf*, Box*);
char	*passauth(char*, char*);
char	*plainauth(char*);
char	*readfile(int);
int	removembox(char*);
int	renamebox(char*, char*, int);
void	resetcurdir(void);
Fetch	*revfetch(Fetch*);
Slist	*revslist(Slist*);
int	searchmsg(Msg*, Search*, int);
int	searchld(Search*);
long	selectfields(char*, long n, char*, Slist*, int);
void	sendflags(Box*, int uids);
void	setflags(Box*, Msg*, int f);
void	setname(char*, ...);
void	setupuser(AuthInfo*);
int	storemsg(Box*, Msg*, int, void*);
char	*strmutf7(char*);
void	strrev(char*, char*);
int	subscribe(char*, int);
int	wrimp(Biobuf*, Box*);
int	appendimp(char*, char*, int, Uidplus*);
void	writeerr(void);
void	writeflags(Biobuf*, Msg*, int);

void	fstreeadd(Box*, Msg*);
void	fstreedelete(Box*, Msg*);
Msg	*fstreefind(Box*, int);
int	fstreecmp(Avl*, Avl*);

#pragma varargck argpos	bye		1
#pragma varargck argpos	debuglog	1
#pragma varargck argpos	imap4cmd	2
#pragma varargck	type	"F"		char*
#pragma varargck	type	"D"		Tm*
#pragma varargck	type	"δ"		Tm*
#pragma varargck	type	"X"		char*
#pragma varargck	type	"Y"		char*
#pragma varargck	type	"Z"		char*

#define	MK(t)		((t*)emalloc(sizeof(t)))
#define	MKZ(t)		((t*)ezmalloc(sizeof(t)))
#define STRLEN(cs)	(sizeof(cs)-1)
