#include "../port/portfns.h"

Dirtab*	addarchfile(char*, int, long(*)(Chan*, void*, long, vlong), 
	long(*)(Chan*, void*, long, vlong));
extern void archreboot(void);
extern void archreset(void);
extern void armtimerset(int);
extern void bootargsinit(uintptr);
extern void cachedwbinv(void);
extern void cachedwbse(void*, int);
extern void cachedwbinvse(void*, int);
extern void cacheiinv(void);
extern void cacheuwbinv(void);
extern void cachedwbtlb(void*, int);
extern void cacheiinvse(void*, int);
extern void cachedinvse(void*, int);
extern uintptr cankaddr(uintptr pa);
extern int cas32(void*, u32int, u32int);
extern void checkmmu(uintptr, uintptr);
extern void clockinit(void);
extern void clockshutdown(void);
extern int cmpswap(long*, long, long);
extern void coherence(void);
extern ulong cprd(int cp, int op1, int crn, int crm, int op2);
extern ulong cprdsc(int op1, int crn, int crm, int op2);
extern void cpuidprint(void);
extern u32int cpidget(void);
extern void cpwr(int cp, int op1, int crn, int crm, int op2, ulong val);
extern void cpwrsc(int op1, int crn, int crm, int op2, ulong val);
#define cycles(ip) *(ip) = lcycles()
extern void dmaflush(int, void*, ulong);
extern void dmastart(int, int, int, void*, void*, int);
extern int dmawait(int);
extern uintptr dmaaddr(void *va);
extern void egpset(uint, int);
extern int fbblank(int);
extern void* fbinit(int, int*, int*, int*);
extern u32int farget(void);
extern void fpon(void);
extern ulong fprd(int fpreg);
extern void fprestreg(int fpreg, uvlong val);
extern void fpsave(FPsave *);
extern ulong fpsavereg(int fpreg, uvlong *fpp);
extern void fpwr(int fpreg, ulong val);
extern u32int fsrget(void);
extern ulong getclkrate(int);
extern char* getconf(char*);
extern char *getethermac(void);
extern uint getfirmware(void);
extern int getpower(int);
extern void getramsize(Confmem*);
extern int getncpus(void);
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
extern u32int ifsrget(void);
extern void intrcpushutdown(void);
extern void intrenable(int, void (*)(Ureg*, void*), void*, int, char*);
extern void intrdisable(int, void (*)(Ureg*, void*), void*, int, char*);
extern void intrsoff(void);
extern int isaconfig(char*, int, ISAConf*);
extern void l2cacheuwbinv(void);
extern void links(void);
extern void mmuinit(void*);
extern void mmuinit1(int);
extern void mmuinvalidate(void);
extern void mmuinvalidateaddr(u32int);
extern uintptr mmukmap(uintptr, uintptr, usize);
extern void okay(int);
extern void procrestore(Proc *);
extern void procsave(Proc*);
extern void procfork(Proc*);
extern void procsetup(Proc*);
extern void screeninit(void);
extern void setclkrate(int, ulong);
extern void setconfenv(void);
extern void setpower(int, int);
extern void setr13(int, u32int*);
extern int startcpus(uint);
extern int splfhi(void);
extern int splflo(void);
extern int tas(void *);
extern void touser(uintptr);
extern void trapinit(void);
extern void uartconsinit(void);
extern int userureg(Ureg*);
extern void vectors(void);
extern void vgpinit(void);
extern void vgpset(uint, int);
extern void vtable(void);
extern void wdogoff(void);
extern void wdogfeed(void);
extern void writeconf(void);
extern void vtable(void);
extern uint getcputemp(void);
extern char *cputype2name(char *buf, int size);
extern uint getboardrev(void);
extern void sev(void);

/*
 * floating point emulation
 */
extern int fpiarm(Ureg*);
extern int fpudevprocio(Proc*, void*, long, uintptr, int);
extern void fpuinit(void);
extern void fpunoted(void);
extern void fpunotify(Ureg*);
extern void fpuprocrestore(Proc*);
extern void fpuprocsave(Proc*);
extern void fpuprocfork(Proc*);
extern void fpusysprocsetup(Proc*);
extern int fpuemu(Ureg*);
/*
 * Things called from port.
 */
extern void delay(int);
extern int islo(void);
extern void microdelay(int);			/* only edf.c */
extern void evenaddr(uintptr);
extern void idlehands(void);
extern void setkernur(Ureg*, Proc*);		/* only devproc.c */
extern void* sysexecregs(uintptr, ulong, int);
extern void sysprocsetup(Proc*);

extern void kexit(Ureg*);

#define	getpgcolor(a)	0
#define	kmapinval()
#define countpagerefs(a, b)

#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))

#define KADDR(pa)	((void*)(KZERO | ((uintptr)(pa) & ~KSEGM)))
#define PADDR(va)	(PHYSDRAM | ((uintptr)(va) & ~KSEGM))

#define MASK(v)	((1UL << (v)) - 1)	/* mask `v' bits wide */
