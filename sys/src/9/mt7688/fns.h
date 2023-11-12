#include "../port/portfns.h"

ulong	cankaddr(ulong);
void	clock(Ureg*);
void	clockinit(void);
int	cmpswap(long*, long, long);
void	coherence(void);
void	cycles(uvlong *);
void	dcflush(void*, ulong);
void	evenaddr(uintptr);
void	faultmips(Ureg*, int, int);
ulong	fcr31(void);
void	fpclear(void);
void	fptrap(Ureg*);
void	fpwatch(Ureg *);
int		fpuemu(Ureg *);
char*	getconf(char*);
ulong	getpagemask(void);
ulong	getrandom(void);
int		gettlbp(ulong, ulong*);
ulong	gettlbvirt(int);
int		isaconfig(char*, int, ISAConf*);
void	icflush(void *, ulong);
void	idlehands(void);
void	introff(int);
void	intron(int);
void	kfault(Ureg*);
KMap*	kmap(Page*);
void	kmapdump(void);
void	kmapinit(void);
void	kmapinval(void);
void	kunmap(KMap*);
void	links(void);
void	outl(void*, void*, ulong);
ulong	prid(void);
void	procfork(Proc *);
void	procrestore(Proc *);
void	procsave(Proc *);
void	procsetup(Proc *);
void	purgetlb(int);
void	puttlbx(int, ulong, ulong, ulong, int);
ulong	rdcompare(void);
ulong	rdcount(void);
ulong*	reg(Ureg*, int);
void	restfpregs(FPsave*, ulong);
void	intrenable(int, void(*)(Ureg *, void *), void *, int, char*);
void	intrdisable(int, void (*)(Ureg*, void *), void*, int, char*);
void	screeninit(void);
void	setpagemask(ulong);
void	setwired(ulong);
ulong	stlbhash(ulong);
void	syscall(Ureg*);
int	tas(void*);
void	tlbinit(void);
ulong	tlbvirt(void);
void	touser(void*);
#define	userureg(ur) ((ur)->status & KUSER)
void	validalign(uintptr, unsigned);
void	wrcompare(ulong);
void	wrcount(ulong);

#define PTR2UINT(p)	((uintptr)(p))
#define UINT2PTR(i)	((void*)(i))

//#define KADDR(a)	((void*)((ulong)(a)|KSEG0))
#define KADDR(pa)	((void*)(KZERO | ((uintptr)(pa) & ~KSEGM)))
//#define PADDR(a)	((ulong)(a)&~KSEGM)
#define PADDR(va)	(((uintptr)(va)) & ~KSEGM)
#define FMASK(o, w)	(((1<<(w))-1)<<(o))

#define KSEG1ADDR(a)	((void*)((ulong)(a)|KSEG1))


void	_uartputs(char*, int);
int		_uartprint(char*, ...);
void	uartconsinit(void);
void	zoot(void);
void	idle(void);
ulong	getstatus(void);
void	setstatus(ulong);
ulong	getcause(void);
ulong	getconfig(void);
ulong	getconfig1(void);
ulong	getconfig2(void);
ulong	getconfig3(void);
ulong	getconfig7(void);
ulong	gethwreg3(void);
void	plan9iniinit(void);
void	noted(Ureg*, ulong);
int		notify(Ureg*);
void	intrinit(void);
int		i8250console(void);
void	setconfenv(void);
void	uartkirkwoodconsole(void);
void	serialputs(char*, int);
void	intrclear(int);

ulong	incraw(void);
ulong	incmask(void);
ulong	incstat(void);
ulong	incsel(void);

void	setwatchhi0(ulong);
void	setwatchlo0(ulong);
void	getfcr0(ulong);
