#pragma	lib	"libaml.a"
#pragma	src	"/sys/src/libaml"

/*
 *	b	uchar*	buffer		amllen() returns number of bytes
 *	s	char*	string		amllen() is strlen()
 *	i	uvlong*	integer
 *	p	void**	package		amllen() is # of elements
 *	r	void*	region
 *	f	void*	field
 *	u	void*	bufferfield
 *	N	void*	name
 *	R	void*	reference
 */
int		amltag(void *);
void*		amlval(void *);
uvlong		amlint(void *);
int		amllen(void *);

void		amlinit(void);
void		amlexit(void);

int		amlload(uchar *data, int len);
void*		amlwalk(void *dot, char *name);
int		amleval(void *dot, char *fmt, ...);
void		amlenum(void *dot, char *seg, int (*proc)(void *, void *), void *arg);

void*		amlroot;
int		amldebug;

#pragma	varargck	type	"V"	void*
#pragma	varargck	type	"N"	void*

/* to be provided by operating system */
extern void*	amlalloc(int);
extern void	amlfree(void*);
