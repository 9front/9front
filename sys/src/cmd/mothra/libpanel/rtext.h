/*
 * Rtext definitions
 */
#define	PL_NOPBIT	4
#define	PL_NARGBIT	12
#define	PL_ARGMASK	((1<<PL_NARGBIT)-1)
#define	PL_SPECIAL(op)	(((-1<<PL_NOPBIT)|op)<<PL_NARGBIT)
#define	PL_OP(t)	((t)&~PL_ARGMASK)
#define	PL_ARG(t)	((t)&PL_ARGMASK)
#define	PL_TAB		PL_SPECIAL(0)		/* # of tab stops before text */
void pltabsize(int, int);			/* set min tab and tab size */
