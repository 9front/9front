#include "../port/portfns.h"

ulong	arcs(ulong, ...);
void	arcsconsinit(void);
void	arcsproc(void*);
void	arcsputc(char);
int	argcgetc(void);
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
void	fptrap(Ureg*);
char*	getconf(char*);
ulong	getpagemask(void);
ulong	getrandom(void);
int	gettlbp(ulong, ulong*);
ulong	gettlbvirt(int);
int	hpc3irqlevel(int);
int	isaconfig(char*, int, ISAConf*);
void	icflush(void *, ulong);
void	idlehands(void);
void	introff(int);
void	intron(int);
void	kfault(Ureg*);
KMap*	kmap(Page*);
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
void	intrenable(int, void(*)(Ureg *, void *), void *);
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

#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))

#define KADDR(a)	((void*)((ulong)(a)|KSEG0))
#define PADDR(a)	((ulong)(a)&~KSEGM)

#define KSEG1ADDR(a)	((void*)((ulong)(a)|KSEG1))
