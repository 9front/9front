#include "../port/portfns.h"

Dirtab*	addarchfile(char*, int, long(*)(Chan*,void*,long,vlong), long(*)(Chan*,void*,long,vlong));
void	archinit(void);
void	bootargs(ulong);
ulong	cankaddr(ulong);
#define	clearmmucache()				/* x86 doesn't have one */
void	clockintr(Ureg*, void*);
int		(*cmpswap)(long*, long, long);
int		cmpswap486(long*, long, long);
void	(*coherence)(void);
void	cpuid(int, ulong regs[]);
int		cpuidentify(void);
void	cpuidprint(void);
void	(*cycles)(uvlong*);
void	delay(int);
#define	evenaddr(x)				/* x86 doesn't care */
void	fpclear(void);
void	fpenv(FPsave*);
void	fpinit(void);
void	fpoff(void);
void	(*fprestore)(FPsave*);
void	(*fpsave)(FPsave*);
void	fpsserestore(FPsave*);
void	fpsserestore0(FPsave*);
void	fpssesave(FPsave*);
void	fpssesave0(FPsave*);
ulong	fpstatus(void);
void	fpx87restore(FPsave*);
void	fpx87save(FPsave*);
ulong	getcr4(void);
char*	getconf(char*);
void	guesscpuhz(int);
void	halt(void);
void	mwait(void*);
void	i8042reset(void);
void	i8253enable(void);
void	i8253init(void);
void	i8253link(void);
uvlong	i8253read(uvlong*);
void	i8253timerset(uvlong);
int	i8259disable(int);
int	i8259enable(Vctl*);
void	i8259init(void);
int	i8259isr(int);
void	i8259on(void);
void	i8259off(void);
int	i8259vecno(int);
void	idle(void);
void	idlehands(void);
int	inb(int);
void	insb(int, void*, int);
ushort	ins(int);
void	inss(int, void*, int);
ulong	inl(int);
void	insl(int, void*, int);
void	intrdisable(int, void (*)(Ureg *, void *), void*, int, char*);
void	intrenable(int, void (*)(Ureg*, void*), void*, int, char*);
int	ioalloc(int, int, int, char*);
void	ioinit(void);
int	isaconfig(char*, int, ISAConf*);
void	kbdenable(void);
#define	kmapinval()
void	links(void);
void	mach0init(void);
void	mathinit(void);
void	mb386(void);
void	mb586(void);
void	mfence(void);
void mmuflushtlb(Page*);
void	mmuinit(void);
ulong	mmukmap(ulong, ulong, int);
int	mmukmapsync(ulong);
#define	mmunewpage(x)
ulong*	mmuwalk(ulong*, ulong, int, int);
char*	mtrr(uvlong, uvlong, char *);
int	mtrrprint(char *, long);
void	mtrrsync(void);
void	outb(int, int);
void	outsb(int, void*, int);
void	outs(int, ushort);
void	outss(int, void*, int);
void	outl(int, ulong);
void	outsl(int, void*, int);
void	printcpufreq(void);
void	procrestore(Proc*);
void	procsave(Proc*);
void	procsetup(Proc*);
void	procfork(Proc*);
void	putcr4(ulong);
int		rdmsr(int, vlong*);
int		screenprint(char*, ...);			/* debugging */
void	(*screenputs)(char*, int);
void	touser(void*);
void	trap(Ureg*);
void	trapenable(int, void (*)(Ureg*, void*), void*, char*);
void	trapinit(void);
int		tas(void*);
#define	userureg(ur) (((ur)->cs & 0xFFFF) == UESEL)
void	vectortable(void);
int		wrmsr(int, vlong);
uint	xchgl(uint*, uint);
uint	xchgw(ushort*, uint);
uint	xchgb(uchar*, uint);
void	rdrandbuf(void*, ulong);

#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))
#define KADDR(a)	((void*)((ulong)(a)|KZERO))
#define PADDR(a)	((ulong)(a)&~KZERO)

#define	dcflush(a, b)

/* Xen functions */
#define rmb()	coherence()
#define wmb()	coherence()
void mb(void);
void hypervisor_callback(void), failsafe_callback(void);
void xenconsinit(void);
ulong mmumapframe(ulong, ulong);
void mmumapcpu0(void);
void dprint(char *, ...);
void xenupdate(ulong *ptr, ulong val);
void xenupdatema(ulong *ptr, uvlong val);
int xenpdptpin(ulong va);
int xenpgdpin(ulong va);
int xenptpin(ulong va);
void xenptunpin(ulong va);
void xenptswitch(ulong pa);
void xentlbflush(void);
int ffs(ulong);
void xengrantinit(void);
int xengrant(domid_t domid, ulong frame, int flags);
int xengrantend(int ref);
void acceptframe(int ref, void *va);
int donateframe(int domid, void *va);
int shareframe(int domid, void *va, int write);
void xenchannotify(int);
void xenupcall(Ureg*);
ulong xenwallclock(void);
int xenstore_read(char*, char*, int);
void xenstore_write(char*, char*);
void xenstore_setd(char *dir, char *node, int value);
int xenstore_gets(char *dir, char *node, char *buf, int buflen);
int xenchanalloc(int);

long HYPERVISOR_set_timer_op(uvlong timeout);
int HYPERVISOR_set_trap_table(trap_info_t *table);
int HYPERVISOR_mmu_update(mmu_update_t *req, int count, int *success_count, domid_t domid);
int HYPERVISOR_mmuext_op(struct mmuext_op *op, int count, int *scount, domid_t domid);
int HYPERVISOR_set_gdt(ulong *frame_list, int entries);
int HYPERVISOR_stack_switch(ulong ss, ulong esp);
int HYPERVISOR_set_callbacks(ulong evss, ulong evfunc, ulong fsss, ulong fsfunc);
int HYPERVISOR_fpu_taskswitch(void);
int HYPERVISOR_yield(void);
int HYPERVISOR_block(void);
int HYPERVISOR_shutdown(int);
int HYPERVISOR_multicall(void *call_list, int nr_calls);
int HYPERVISOR_event_channel_op(void *op);
int HYPERVISOR_xen_version(int cmd, void *arg);
int HYPERVISOR_console_io(int cmd, int count, char *str);
int HYPERVISOR_grant_table_op(int cmd, gnttab_setup_table_t *setup, int count);
int HYPERVISOR_memory_op(int cmd, struct xen_memory_reservation *arg);
