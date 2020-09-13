enum {
	IrqCLOCK	= 0,
	IrqKBD		= 1,
	IrqUART1	= 3,
	IrqUART0	= 4,
	IrqPCMCIA	= 5,
	IrqFLOPPY	= 6,
	IrqLPT		= 7,
	IrqIRQ7		= 7,
	IrqAUX		= 12,		/* PS/2 port */
	IrqIRQ13	= 13,		/* coprocessor on 386 */
	IrqATA0		= 14,
	IrqATA1		= 15,
	MaxIrqPIC	= 15,

	VectorPIC	= 32,
	MaxVectorPIC	= VectorPIC+MaxIrqPIC,
};

typedef struct Vctl {
	Vctl*	next;			/* handlers on this vector */

	char	name[KNAMELEN];	/* of driver */
	int	isintr;			/* interrupt or fault/trap */
	int	irq;
	int	tbdf;
	int	(*isr)(int);		/* get isr bit for this irq */
	int	(*eoi)(int);		/* eoi */

	void	(*f)(Ureg*, void*);	/* handler to call */
	void*	a;			/* argument to call it with */
} Vctl;

enum {
	MaxEISA		= 16,
	EISAconfig	= 0xC80,
};

#define PCIWINDOW	0x80000000
#define PCIWADDR(va)	(PADDR(va)+PCIWINDOW)

#define BUSUNKNOWN	(-1)
