#pragma varargck type "M"	Msg*
#pragma varargck type "P"	Kvp*
#pragma varargck type "K"	Key*
#pragma varargck type "V"	Val*
#pragma varargck type "B"	Bptr
#pragma varargck type "R"	Arange*
#pragma varargck type "X"	char*
#pragma varargck type "Q"	Qid

extern Gefs*	fs;
extern int	debug;
extern int	permissive;
extern int	usereserve;
extern char*	reamuser;
extern Errctx**	errctx;
extern Blk*	blkbuf;
extern int	noneid;
extern int	nogroupid;
extern int	admid;
extern int	fuzzonly;
extern ulong	fuzzseed;

#define	UNPACK8(p)	(((uchar*)(p))[0])
#define	UNPACK16(p)	((((uchar*)(p))[0]<<8)|(((uchar*)(p))[1]))
#define	UNPACK32(p)	((((uchar*)(p))[0]<<24)|(((uchar*)(p))[1]<<16)|\
				(((uchar*)(p))[2]<<8)|(((uchar*)(p))[3]))
#define	UNPACK64(p)	(((u64int)((((uchar*)(p))[0]<<24)|(((uchar*)(p))[1]<<16)|\
				(((uchar*)(p))[2]<<8)|(((uchar*)(p))[3])))<<32 |\
			((u64int)((((uchar*)(p))[4]<<24)|(((uchar*)(p))[5]<<16)|\
				(((uchar*)(p))[6]<<8)|(((uchar*)(p))[7]))))

#define	PACK8(p,v)	do{(p)[0]=(v);}while(0)
#define	PACK16(p,v)	do{(p)[0]=(v)>>8;(p)[1]=(v);}while(0)
#define	PACK32(p,v)	do{(p)[0]=(v)>>24;(p)[1]=(v)>>16;(p)[2]=(v)>>8;(p)[3]=(v);}while(0)
#define	PACK64(p,v)	do{(p)[0]=(v)>>56;(p)[1]=(v)>>48;(p)[2]=(v)>>40;(p)[3]=(v)>>32;\
			   (p)[4]=(v)>>24;(p)[5]=(v)>>16;(p)[6]=(v)>>8;(p)[7]=(v);}while(0)

void*	emalloc(usize, int);

Blk*	newdblk(Tree*, vlong, int);
Blk*	newblk(Tree*, int);
Blk*	dupblk(Tree*, Blk*);
Blk*	getroot(Tree*, int*);
Blk*	getblk(Bptr, int);
Blk*	holdblk(Blk*);
void	dropblk(Blk*);

void	lrutop(Blk*);
void	lrubot(Blk*);
void	cacheins(Blk*);
void	cachedel(vlong);
Blk*	cacheget(vlong);
Blk*	cachepluck(void);

void	qinit(Syncq*);
void	qput(Syncq*, Qent);

Arena*	getarena(vlong);
void	syncblk(Blk*);
void	enqueue(Blk*);
void	epochstart(int);
void	epochend(int);
void	epochwait(void);
void	epochclean(void);
void	limbo(int op, Limbo*);
void	freeblk(Tree*, Blk*);
void	freebp(Tree*, Bptr);
int	logbarrier(Arena *, vlong);
void	killblk(Tree*, Bptr);
ushort	blkfill(Blk*);
uvlong	blkhash(Blk*);
uvlong	bufhash(void*, usize);
u32int	ihash(uvlong);
void	finalize(Blk*);

Mount*	getmount(char*);
void	clunkmount(Mount*);

Tree*	updatesnap(Tree*, char*, int);
void	tagsnap(Tree*, char*, int);
void	delsnap(Tree*, vlong, char*);
void	freedl(Dlist*, int);
Tree*	opensnap(char*, int*);

void	closesnap(Tree*);
void	reamfs(char*);
void	growfs(char*);
void	loadarena(Arena*, Bptr);
void	loadfs(char*);
void	loadlog(Arena*, Bptr);
void	flushlog(Arena*);
int	scandead(Dlist*, int, void(*)(Bptr, void*), void*);
int	endfs(void);
void	compresslog(Arena*);
void	dlsync(void);
void	setval(Blk*, Kvp*);

Conn*	newconn(int, int, int);
void	putconn(Conn*);

int	walk1(Tree*, vlong, char*, Qid*, vlong*);
void	loadusers(int, Tree*);
User*	uid2user(int);
User*	name2user(char*);

void	btupsert(Tree*, Msg*, int);
int	btlookup(Tree*, Key*, Kvp*, char*, int);
void	btnewscan(Scan*, char*, int);
void	btenter(Tree*, Scan*);
int	btnext(Scan*, Kvp*);
void	btexit(Scan*);

int	checkflag(Blk *b, int, int);
void	setflag(Blk *b, int, int);

char*	estrdup(char*);

int	keycmp(Key *, Key *);
void	cpkey(Key*, Key*, char*, int);
void	cpkvp(Kvp*, Kvp*, char*, int);

/* for dumping */
void	getval(Blk*, int, Kvp*);
void	getmsg(Blk*, int, Msg*);
Bptr	getptr(Kvp*, int*);

void	initshow(void);
void	showblk(int, Blk*, char*, int);
void	showbp(int, Bptr, int);
void	showtreeroot(int, Tree*);
int	checkfs(int);


#define dprint(...) \
		if(debug) fprint(2, __VA_ARGS__)

#define fatal(...) \
	fprint(2, __VA_ARGS__), abort()

#define tracex(msg, bp, v0, v1) \
		if(fs->trace != nil) _trace(msg, bp, v0, v1)

#define bassert(b, cond) \
	if(cond){}else _babort(b->bp.addr, "cond")

#define traceb(msg, bp)	tracex(msg, bp, -1, -1)
#define tracev(msg, v0)	tracex(msg, Zb, v0, -1)
#define tracem(msg)	tracex(msg, Zb, -1, -1)

jmp_buf*	_waserror(void);
_Noreturn void	error(char*, ...);
_Noreturn void	broke(char*, ...);
_Noreturn void	nexterror(void);
_Noreturn void	_babort(vlong, char*);
#define waserror()	(setjmp(*_waserror()))
#define errmsg()	((*errctx)->err)
#define	poperror()	assert((*errctx)->nerrlab-- > 0)
#define estacksz()	((*errctx)->nerrlab)
void	writetrace(void*, vlong);
void	_trace(char*, Bptr, vlong, vlong);
char*	packstr(char*, char*, char*);

void	dir2kv(vlong, Xdir*, Kvp*, char*, int);
int	dir2statbuf(Xdir*, char*, int);
void	dlist2kv(Dlist*, Kvp*, char*, int);
void	lbl2kv(char*, vlong, uint, Kvp*, char*, int);
void	link2kv(vlong, vlong, Kvp*, char*, int);
void	retag2kv(vlong, vlong, int, int, Kvp*, char*, int);
void	tree2kv(Tree*, Kvp*, char*, int);

void	kv2dir(Kvp*, Xdir*);
void	kv2dlist(Kvp*, Dlist*);
void	kv2link(Kvp*, vlong*, vlong*);
void	kv2qid(Kvp*, Qid*);
int	kv2statbuf(Kvp*, char*, int);

char*	packarena(char*, int, Arena*);
char*	packbp(char*, int, Bptr*);
char*	packdkey(char*, int, vlong, char*);
char*	packdval(char*, int, Xdir*);
char*	packlbl(char*, int, char*);
char*	packsnap(char*, int, vlong);
char*	packsuper(char*, int, vlong);
char*	packtree(char*, int, Tree*);
char*	packsb(char*, int, Gefs*);

char*	unpackarena(Arena*, char*, int);
Bptr	unpackbp(char*, int);
char*	unpackdkey(char*, int, vlong*);
Tree*	unpacktree(Tree*, char*, int);
char*	unpacksb(Gefs*, char*, int);
char*	unpackstr(char*, char*, char**);

/* fmt */
int	Bconv(Fmt*);
int	Mconv(Fmt*);
int	Pconv(Fmt*);
int	Rconv(Fmt*);
int	Kconv(Fmt*);
int	Qconv(Fmt*);

Chan*	mkchan(int);
void*	chrecv(Chan*);
void	chsend(Chan*, void*);
int	chsendnb(Chan*, void*, int);
void	runfs(int, void*);
void	runmutate(int, void*);
void	runread(int, void*);
void	runcons(int, void*);
void	runtasks(int, void*);
void	runsync(int, void*);
void	runsweep(int, void*);
void	runsnap(int, void*);

/* fuzzing and testing */
void	fzinit(void);
void	fzwrite(int, void*);
void	fzscan(int, void*);

