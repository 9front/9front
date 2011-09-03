#include "../port/portfns.h"

#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))
#define getpgcolor(x)	0
#define kmapinval()
#define checkmmu(a,b)
#define userureg(ur)	(((ur)->psr & 0x1F) == 0x10)

extern void procsave(Proc *);
extern void procrestore(Proc *);
extern void idlehands(void);
extern void coherence(void);
extern int tas(void*);
extern int cmpswap(long*, long, long);
extern void evenaddr(uintptr);
extern void procsetup(Proc*);
extern void procfork(Proc*);
extern uintptr cankaddr(uintptr);
extern void* KADDR(ulong);
extern ulong paddr(void *);
extern void cycles(uvlong *);
extern void markidle(int);
#define PADDR(x) paddr((void*)(x))

void	mmuinit(void);
void	flushtlb(void);
void	trapinit(void);
void*	vmap(ulong, ulong);
void	vunmap(void*, ulong);
void	printureg(Ureg*);
void	fillureguser(Ureg*);
void	touser(Ureg*);
void	links(void);
void	globalclockinit(void);
void	localclockinit(void);
void	intenable(int, void(*)(Ureg*));
void	setled(int, int);
void	uartinit(void);
void	irqroute(int, void(*)(Ureg*));
