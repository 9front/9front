#include "../port/portfns.h"

#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))

/* l.s */
extern void sev(void);
extern int tas(void *);
extern int cmpswap(long*, long, long);
extern void coherence(void);
extern void idlehands(void);
extern uvlong cycles(void);
extern int splfhi(void);
extern void splflo(void);
extern void touser(uintptr sp);
extern void forkret(void);
extern void noteret(void);
extern void returnto(void*);
extern void fpsaveregs(void*);
extern void fploadregs(void*);
extern void magic(void);

extern void setttbr(uintptr pa);
extern uintptr getfar(void);

extern void flushasidva(uintptr asidva);
extern void tlbivae1is(uintptr asidva);

extern void flushasidvall(uintptr asidva);
extern void tlbivale1is(uintptr asidva);

extern void flushasid(uintptr asid);
extern void tlbiaside1is(uintptr asid);

extern void flushtlb(void);
extern void tlbivmalle1(void);

/* cache */
extern ulong cachesize(int level);

extern void cacheiinvse(void*, int);
extern void cacheuwbinv(void);
extern void cacheiinv(void);

extern void cachedwbse(void*, int);
extern void cacheduwbse(void*, int);
extern void cachedinvse(void*, int);
extern void cachedwbinvse(void*, int);

extern void cachedwb(void);
extern void cachedinv(void);
extern void cachedwbinv(void);

extern void l2cacheuwb(void);
extern void l2cacheuinv(void);
extern void l2cacheuwbinv(void);

/* mmu */
#define	getpgcolor(a)	0
extern uintptr paddr(void*);
#define PADDR(a) paddr((void*)(a))
extern uintptr cankaddr(uintptr);
extern void* kaddr(uintptr);
#define KADDR(a) kaddr(a)
extern void kmapinval(void);
#define	VA(k)	((uintptr)(k))
extern KMap *kmap(Page*);
extern void kunmap(KMap*);
extern uintptr mmukmap(uintptr, uintptr, usize);

extern void mmu0init(uintptr*);
extern void mmu0clear(uintptr*);
extern void mmu1init(void);

extern void putasid(Proc*);

/* clock */
extern void clockinit(void);
extern void synccycles(void);
extern void armtimerset(int);

/* fpu */
extern void fpuinit(void);
extern void fpoff(void);
extern void fpinit(void);
extern void fpclear(void);
extern void fpsave(FPsave*);
extern void fprestore(FPsave*);
extern void mathtrap(Ureg*);

/* trap */
extern void trapinit(void);
extern int userureg(Ureg*);
extern void evenaddr(uintptr);
extern void setkernur(Ureg*, Proc*);
extern void procfork(Proc*);
extern void procsetup(Proc*);
extern void procsave(Proc*);
extern void procrestore(Proc *);
extern void trap(Ureg*);
extern void syscall(Ureg*);
extern void noted(Ureg*, ulong);
extern void faultarm64(Ureg*);
extern void dumpstack(void);
extern void dumpregs(Ureg*);

/* irq */
extern void intrcpushutdown(void);
extern void intrsoff(void);
#define intrenable(i, f, a, b, n)	irqenable((i), (f), (a))
extern void irqenable(int, void (*)(Ureg*, void*), void*);
extern int irq(Ureg*);
extern void fiq(Ureg*);

/* sysreg */
extern uvlong	sysrd(ulong);
extern void	syswr(ulong, uvlong);

/* gpio */
extern void gpiosel(uint, int);
extern void gpiopull(uint, int);
extern void gpiopullup(uint);
extern void gpiopulloff(uint);
extern void gpiopulldown(uint);
extern void gpioout(uint, int);
extern int gpioin(uint);
extern void gpioselevent(uint, int, int);
extern int gpiogetevent(uint);
extern void gpiomeminit(void);

/* arch */
extern char *cputype2name(char*, int);
extern void cpuidprint(void);
extern void uartconsinit(void);
extern void links(void);
extern int getncpus(void);
extern int startcpu(uint);
extern void okay(int);

/* dma */
extern uintptr dmaaddr(void*);
extern void dmastart(int, int, int, void*, void*, int);
extern int dmawait(int);

/* vcore */
extern void* fbinit(int set, int *width, int *height, int *depth);
extern int fbblank(int blank);
extern void setpower(int dev, int on);
extern int getpower(int dev);
extern char* getethermac(void);
extern uint getboardrev(void);
extern uint getfirmware(void);
extern void getramsize(Confmem *mem);
extern ulong getclkrate(int clkid);
extern void setclkrate(int clkid, ulong hz);
extern uint getcputemp(void);
extern void vgpinit(void);
extern void vgpset(uint port, int on);

/* bootargs */
extern void bootargsinit(void);
extern char *getconf(char *name);
extern void setconfenv(void);
extern void writeconf(void);

/* screen */
extern void screeninit(void);

extern int isaconfig(char*, int, ISAConf*);
