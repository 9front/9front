#include "../port/portfns.h"

ulong	cankaddr(ulong);
void	clockinit(void);
void	clockintr(Ureg*);
void	clockintrsched(void);
int	cmpswap(long*, long, long);
#define coherence()	eieio()
void	cpuidprint(void);
#define cycles(x)	do{}while(0)
void	dcflush(void*, ulong);
void	delay(int);
void	dumpregs(Ureg*);
void	delayloopinit(void);
void	eieio(void);
void	evenaddr(ulong);
void	faultpower(Ureg*, ulong addr, int read);
void	fprestore(FPsave*);
void	fpsave(FPsave*);
char*	getconf(char*);
ulong	getdar(void);
ulong	getdec(void);
ulong	getdsisr(void);
ulong	gethid0(void);
ulong	gethid1(void);
ulong	getmsr(void);
ulong	getpvr(void);
void	gotopc(ulong);
int	havetimer(void);
void	hwintrinit(void);
void	i8250console(void);
void	i8259init(void);
int	i8259intack(void);
int	i8259enable(Vctl*);
int	i8259vecno(int);
int	i8259disable(int);
void	icflush(void*, ulong);
#define	idlehands()			/* nothing to do in the runproc */
int	inb(int);
void	insb(int, void*, int);
ushort	ins(int);
void	inss(int, void*, int);
ulong	inl(int);
void	insl(int, void*, int);
void	intr(Ureg*);
void	intrenable(int, void (*)(Ureg*, void*), void*, int, char*);
void	ioinit(void);
int	iprint(char*, ...);
int	isaconfig(char*, int, ISAConf*);
#define kexit(a)
#define	kmapinval()
void	links(void);
void	mmuinit(void);
void	mmusweep(void*);
void	mpicdisable(int);
void	mpicenable(int, Vctl*);
int	mpiceoi(int);
int	mpicintack(void);
int	newmmupid(void);
void	outb(int, int);
void	outsb(int, void*, int);
void	outs(int, ushort);
void	outss(int, void*, int);
void	outl(int, ulong);
void	outsl(int, void*, int);
int	pcicfgrw8(int, int, int, int);
int	pcicfgrw16(int, int, int, int);
int	pcicfgrw32(int, int, int, int);
#define procrestore(p)
void	procsave(Proc*);
void	procsetup(Proc*);
void	procfork(Proc*);
void	putdec(ulong);
void	puthid0(ulong);
void	puthid1(ulong);
void	putmsr(ulong);
void	putsdr1(ulong);
void	putsr(int, ulong);
void	raveninit(void);
void	sync(void);
int	tas(void*);
void	timeradd(Timer *);
void	timerdel(Timer *);
void	touser(void*);
void	trapinit(void);
void	trapvec(void);
void	tlbflush(ulong);
void	tlbflushall(void);
#define	userureg(ur) (((ur)->status & MSR_PR) != 0)
void	watchreset(void);

#define KADDR(a)	((void*)((ulong)(a)|KZERO))
#define PADDR(a)	((ulong)(a)&~KZERO)
