/*
 * xensystem.c
 *
 * TODO: we could handle mmu updates more efficiently by
 * using a multicall.
 * XXX perhaps we should check return values and panic on failure?
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"

#define LOG(a)

/*
 * These functions replace all the inlines that are used on Linux systems
 */

/* in xen.s */
int xencall1(int op);
int xencall2(int op, ulong arg1);
int xencall3(int op, ulong arg1, ulong arg2);
int xencall4(int op, ulong arg1, ulong arg2, ulong arg3);
int xencall5(int op, ulong arg1, ulong arg2, ulong arg3, ulong arg4);
int xencall6(int op, ulong arg1, ulong arg2, ulong arg3, ulong arg4, ulong arg5);

int
HYPERVISOR_update_va_mapping(ulong va, uvlong newval, ulong flags)
{
	int ret;

	ret = xencall5(__HYPERVISOR_update_va_mapping, va, newval, newval>>32, flags);
	if(ret < 0)
		panic("update_va_mapping failed");
	return ret;
}

long
HYPERVISOR_set_timer_op(uvlong timeout)
{
	ulong hi, lo;

	hi = timeout>>32;
	lo = timeout;
	return xencall3(__HYPERVISOR_set_timer_op, lo, hi);
}

int 
HYPERVISOR_set_trap_table(trap_info_t *table)
{
	return xencall2(__HYPERVISOR_set_trap_table, (ulong)table);
}

int
HYPERVISOR_mmu_update(mmu_update_t *req, int count,
	int *success_count, domid_t domid)
{
	return xencall5(__HYPERVISOR_mmu_update, (ulong)req, count, (ulong)success_count, domid);
}

int
HYPERVISOR_mmuext_op(struct mmuext_op *op, int count, int *scount, domid_t domid)
{
	return xencall5(__HYPERVISOR_mmuext_op, (ulong)op, count, (ulong)scount, domid);
}

int 
HYPERVISOR_set_gdt(unsigned long *frame_list, int entries)
{
	return xencall3(__HYPERVISOR_set_gdt, (ulong)frame_list, entries);
}

int
HYPERVISOR_stack_switch(ulong ss, ulong esp)
{
	return xencall3(__HYPERVISOR_stack_switch, ss, esp);
}

/* XXX match evfunc and fsfunc prototypes? */
int
HYPERVISOR_set_callbacks(ulong evss, ulong evfunc, ulong fsss, ulong fsfunc)
{
	return xencall5(__HYPERVISOR_set_callbacks, evss, evfunc, fsss, fsfunc);
}

int
HYPERVISOR_fpu_taskswitch(void)
{
	return xencall1(__HYPERVISOR_fpu_taskswitch);
}

int
HYPERVISOR_yield(void)
{
	return xencall3(__HYPERVISOR_sched_op, SCHEDOP_yield, 0);
}

int
HYPERVISOR_block(void)
{
	return xencall3(__HYPERVISOR_sched_op, SCHEDOP_block, 0);
}

int 
HYPERVISOR_shutdown(int reboot)
{
	sched_shutdown_t arg;

	arg.reason = reboot? SHUTDOWN_reboot : SHUTDOWN_poweroff;
	return xencall3(__HYPERVISOR_sched_op, SCHEDOP_shutdown, (ulong)&arg);
}

int
HYPERVISOR_multicall(void *call_list, int nr_calls)
{
	return xencall3(__HYPERVISOR_multicall, (ulong)call_list, nr_calls);
}


int 
HYPERVISOR_event_channel_op(void *op)
{
	return xencall2(__HYPERVISOR_event_channel_op, (ulong)op);
}

int
HYPERVISOR_xen_version(int cmd, void *arg)
{
	return xencall3(__HYPERVISOR_xen_version, cmd, (ulong)arg);
}

int
HYPERVISOR_console_io(int cmd, int count, char *str)
{
	return xencall4(__HYPERVISOR_console_io, cmd, count, (ulong)str);
}

int
HYPERVISOR_grant_table_op(int cmd, gnttab_setup_table_t *setup, int count)
{
	return xencall4(__HYPERVISOR_grant_table_op, cmd, (ulong)setup, count);
}

int
HYPERVISOR_memory_op(int cmd, struct xen_memory_reservation *arg)
{
	return xencall3(__HYPERVISOR_memory_op, cmd, (ulong)arg);
}

/* 
 * XXX this comment is leftover from old code.  revisit and update.
 *
 * The use of 'barrier' in the following reflects their use as local-lock
 * operations. Reentrancy must be prevented (e.g., __cli()) /before/ following
 * critical operations are executed. All critical operatiosn must complete
 * /before/ reentrancy is permitted (e.g., __sti()). Alpha architecture also
 * includes these barriers, for example.
 */

/*
 * conversions to machine page numbers, pages and addresses
 */
#define MFN(pa)		(patomfn[(pa)>>PGSHIFT])
#define MFNPG(pa)		((uvlong)MFN(pa)<<PGSHIFT)
#define PA2MA(pa)		(MFNPG(pa) | PGOFF(pa))
#define VA2MA(va)		PA2MA(PADDR(va))
#define VA2MFN(va)		MFN(PADDR(va))

ulong hypervisor_virt_start;
ulong xentop;
start_info_t *xenstart;
shared_info_t *HYPERVISOR_shared_info;
ulong *patomfn;
ulong *matopfn;

int
xenpdptpin(ulong va)
{
	struct mmuext_op op;
	ulong mfn;

	mfn = MFN(PADDR(va));
	LOG(dprint("pdptpin %lux %lux\n", va, mfn);)
	print("pdptpin %lux %lux\n", va, mfn);
	/* mark page readonly first */
	HYPERVISOR_update_va_mapping(va, ((uvlong)mfn<<PGSHIFT)|PTEVALID, UVMF_INVLPG|UVMF_LOCAL);

	/*  L3 here refers to page directory pointer table (PAE mode) */
	op.cmd = MMUEXT_PIN_L3_TABLE;
	op.arg1.mfn = mfn;
	if (HYPERVISOR_mmuext_op(&op, 1, 0, DOMID_SELF) == 0)
		return 1;
	HYPERVISOR_update_va_mapping(va, ((uvlong)mfn<<PGSHIFT)|PTEVALID|PTEWRITE, UVMF_INVLPG|UVMF_LOCAL);
	return 0;
}

int
xenpgdpin(ulong va)
{
	struct mmuext_op op;
	ulong mfn;

	mfn = MFN(PADDR(va));
	LOG(dprint("pdpin %lux %lux\n", va, mfn);)
	/* mark page readonly first */
	HYPERVISOR_update_va_mapping(va, ((uvlong)mfn<<PGSHIFT)|PTEVALID, UVMF_INVLPG|UVMF_LOCAL);

	/* to confuse you, L2 here refers to page directories */
	op.cmd = MMUEXT_PIN_L2_TABLE;
	op.arg1.mfn = mfn;
	if (HYPERVISOR_mmuext_op(&op, 1, 0, DOMID_SELF) == 0)
		return 1;
	HYPERVISOR_update_va_mapping(va, ((uvlong)mfn<<PGSHIFT)|PTEVALID|PTEWRITE, UVMF_INVLPG|UVMF_LOCAL);
	return 0;
}

int
xenptpin(ulong va)
{
	struct mmuext_op op;
	ulong mfn;

	mfn = MFN(PADDR(va));
	LOG(dprint("pin %lux %lux\n", va, mfn);)
	/* mark page readonly first */
	HYPERVISOR_update_va_mapping(va, ((uvlong)mfn<<PGSHIFT)|PTEVALID, UVMF_INVLPG|UVMF_LOCAL);

	/* to confuse you, L1 here refers to page tables */
	op.cmd = MMUEXT_PIN_L1_TABLE;
	op.arg1.mfn = mfn;
	if (HYPERVISOR_mmuext_op(&op, 1, 0, DOMID_SELF) == 0)
		return 1;
	HYPERVISOR_update_va_mapping(va, ((uvlong)mfn<<PGSHIFT)|PTEVALID|PTEWRITE, UVMF_INVLPG|UVMF_LOCAL);
	return 0;
}

void
xenptunpin(ulong va)
{
	struct mmuext_op op;
	ulong mfn;

	mfn = MFN(PADDR(va));
	LOG(dprint("unpin %lux %lux\n", va, mfn);)
	op.cmd = MMUEXT_UNPIN_TABLE;
	op.arg1.mfn = mfn;
	if(HYPERVISOR_mmuext_op(&op, 1, 0, DOMID_SELF)<0)
		panic("xenptunpin va=%lux called from %lux", va, getcallerpc(&va));

	/* mark page read-write */
	HYPERVISOR_update_va_mapping(va, ((uvlong)mfn<<PGSHIFT)|PTEVALID|PTEWRITE, UVMF_INVLPG|UVMF_LOCAL);
}

void
xenptswitch(ulong pa)
{
	struct mmuext_op op;

	op.cmd = MMUEXT_NEW_BASEPTR;
	op.arg1.mfn = MFN(pa);
	if(HYPERVISOR_mmuext_op(&op, 1, 0, DOMID_SELF)<0)
		panic("xenptswitch");
}

void
xentlbflush(void)
{
	struct mmuext_op op;

	op.cmd = MMUEXT_TLB_FLUSH_LOCAL;
	HYPERVISOR_mmuext_op(&op, 1, 0, DOMID_SELF);
}

/* update a pte using a machine page frame number */
void 
xenupdatema(ulong *ptr, uvlong val)
{
	mmu_update_t u;

	u.ptr = VA2MA(ptr);
	u.val = val;
	if(HYPERVISOR_mmu_update(&u, 1, 0, DOMID_SELF) < 0)
		panic("xenupdatema - pte %lux value %llux (was %llux) called from %lux", (ulong)ptr, val, *(uvlong*)ptr, getcallerpc(&ptr));
}

/* update a pte using a guest "physical" page number */
void 
xenupdate(ulong *ptr, ulong val)
{
	mmu_update_t u;

	u.ptr = VA2MA(ptr);
	u.val = PA2MA(val);
	if(HYPERVISOR_mmu_update(&u, 1, 0, DOMID_SELF) < 0)
		panic("xenupdate - pte %lux value %lux (%llux) called from %lux", (ulong)ptr, val, PA2MA(val), getcallerpc(&ptr));
}

void
acceptframe(int ref, void *va)
{
	ulong mfn;

	mfn = xengrantend(ref);
	if (mfn == 0)
		panic("can't accept page frame");
	LOG(dprint("acceptframe ref %d va %lux mfn %lux\n", ref, (ulong)va, mfn);)
	VA2MFN(va) = mfn;
	mmumapframe((ulong)va, mfn);
}

int
donateframe(int domid, void *va)
{
	ulong mfn;
	int ref;
	ulong *pte;
	struct xen_memory_reservation mem;

	mfn = VA2MFN(va);
	ref = xengrant(domid, mfn, GTF_accept_transfer);
	LOG(dprint("grant transfer %lux (%lux) -> %d\n", (ulong)va, mfn, ref);)
	pte = mmuwalk(m->pdb, (ulong)va, 2, 0);
	xenupdatema(pte, 0);
	set_xen_guest_handle(mem.extent_start, &mfn);
	mem.nr_extents = 1;
	mem.extent_order = 0;
	mem.address_bits = 0;
	mem.domid = DOMID_SELF;
	if (HYPERVISOR_memory_op(XENMEM_decrease_reservation, &mem) != 1)
		panic("XENMEM_decrease_reservation");
	VA2MFN(va) = ~0;
	return ref;
}

int
shareframe(int domid, void *va, int write)
{
	ulong mfn;
	int ref;
	int flags;

	mfn = VA2MFN(va);
	flags = GTF_permit_access;
	if (!write)
		flags |= GTF_readonly;
	ref = xengrant(domid, mfn, flags);
	LOG(dprint("grant shared %lux (%lux) -> %d\n", (ulong)va, mfn, ref);)
	return ref;
}

/*
 * Upcall from hypervisor, entered with evtchn_upcall_pending masked.
 */
void
xenupcall(Ureg *ureg)
{
	vcpu_info_t *vcpu;
	shared_info_t *s;
	ulong sel1, sel2, n1, n2, port;

	ureg->ecode = 0;
	s = HYPERVISOR_shared_info;
	vcpu = &HYPERVISOR_shared_info->vcpu_info[0];
	for (;;) {
		vcpu->evtchn_upcall_pending = 0;
		sel1 = xchgl((uint*)&vcpu->evtchn_pending_sel, 0);
		while(sel1) {
			n1 = ffs(sel1);
			sel1 &= ~(1<<n1);
			sel2 = xchgl((uint*)&s->evtchn_pending[n1], 0);
			while(sel2) {
				n2 = ffs(sel2);
				sel2 &= ~(1<<n2);
				port = (n1<<5) + n2;
				ureg->trap = 100+port;
				trap(ureg);
			}
		}
		if (vcpu->evtchn_upcall_pending)
			continue;
		vcpu->evtchn_upcall_mask = 0;
		if (vcpu->evtchn_upcall_pending == 0)
			break;
		vcpu->evtchn_upcall_mask = 1;
	}
	
}

/*
 * tbdf field is abused to distinguish virqs from channels:
 *
 * tbdf=BUSUNKNOWN -> irq is a virq to be bound to a channel
 * tbdf=0 -> irq is a channel number
 */
int
xenintrenable(Vctl *v)
{
	evtchn_op_t op;
	uint port;

	/* XXX locking? */
	if (v->tbdf != BUSUNKNOWN) {
		op.cmd = EVTCHNOP_bind_virq;
		op.u.bind_virq.virq = v->irq;
		op.u.bind_virq.vcpu = m->machno;
		if(HYPERVISOR_event_channel_op(&op) != 0)
			panic("xenintrenable: bind %d failed", v->irq);
		port = op.u.bind_virq.port;
	} else
		port = v->irq;
	if (port > 155)
		return -1;
	HYPERVISOR_shared_info->evtchn_mask[port/32] &= ~(1<<(port%32));
	if(0)print("xenintrenable %s: irq %d port %d mask[%d] = %#lux\n", v->name, v->irq, port, port/32, HYPERVISOR_shared_info->evtchn_mask[port/32]);
	return 100+port;
}

int
xenintrdisable(int irq)
{
	USED(irq);
	panic("xenintrdisable notyet\n");
	return 0;
}

int
xenintrvecno(int irq)
{
	return irq;
}

int
islo(void)
{
	vcpu_info_t *cpu;

	cpu = &HYPERVISOR_shared_info->vcpu_info[m->machno];	// XXX m->shared
	return (cpu->evtchn_upcall_mask == 0);
}

/*
 * Note: Portable code expects spllo <= spl* <= spldone for
 * accounting purposes.  Lets hope the compiler doesn't reorder
 * us.
 */
int 
spllo(void)
{
	vcpu_info_t *cpu = &HYPERVISOR_shared_info->vcpu_info[m->machno];	// XXX m->shared

	if(cpu->evtchn_upcall_mask == 0)
		return 0;
	m->splpc = 0;
	cpu->evtchn_upcall_mask = 0;

	/*
	 * If an event arrived while masked off,
	 * use a dummy call to trigger delivery
	 */
	if (cpu->evtchn_upcall_pending)
		HYPERVISOR_xen_version(0, 0);

	return 1;
}

int 
splhi(void)
{
	ulong dummy;
	vcpu_info_t *cpu = &HYPERVISOR_shared_info->vcpu_info[m->machno];	// XXX m->shared
	int oldmask;

	oldmask = xchgb(&cpu->evtchn_upcall_mask, 1);
   	if (cpu->evtchn_upcall_mask != 1)
		panic("xchgb");
	/* XXX ad-hoc ¨getcallerpc" because we have no arguments */
	m->splpc = (&dummy)[1];
	return oldmask;
}

void
splx(int x)
{
	if(x)
		splhi();
	else
		spllo();
}

/* marker for profiling in portable code */
void
spldone(void)
{
}

/* allocate an event channel */
int
xenchanalloc(int dom)
{
	evtchn_op_t op;

	op.cmd = EVTCHNOP_alloc_unbound;
	op.u.alloc_unbound.dom = DOMID_SELF;
	op.u.alloc_unbound.remote_dom = dom;
	if (HYPERVISOR_event_channel_op(&op) != 0)
		panic("xenchanalloc");
	return op.u.alloc_unbound.port;
}

/* notify over an event channel */
void
xenchannotify(int port)
{
	evtchn_op_t op;

	op.cmd = EVTCHNOP_send;
	op.u.send.port = port;
	HYPERVISOR_event_channel_op(&op);
}

void
halt(void)
{
	extern int nrdy;

	splhi();
	if (nrdy) {
		spllo();
		return;
	}
	HYPERVISOR_block();
}

void
mb(void)
{
	coherence();
}
