/* devmouse.c */
typedef struct Cursor Cursor;
extern Cursor cursor;
extern void mousetrack(int, int, int, ulong);
extern void absmousetrack(int, int, int, ulong);
extern Point mousexy(void);
extern void mouseaccelerate(int);

/* screen.c */
extern void*	screeninit(int width, int hight, int depth);
extern void	blankscreen(int);
extern void	flushmemscreen(Rectangle);
extern Memdata*	attachscreen(Rectangle*, ulong*, int*, int*, int*);
extern void	cursoron(void);
extern void	cursoroff(void);
extern void	setcursor(Cursor*);

extern void mousectl(Cmdbuf*);
extern void mouseresize(void);
extern void mouseredraw(void);

/* devdraw.c */
extern QLock	drawlock;

#define ishwimage(i)	1		/* for ../port/devdraw.c */

/* swcursor.c */
void		swcursorhide(int);
void		swcursoravoid(Rectangle);
void		swcursordraw(Point);
void		swcursorload(Cursor *);
void		swcursorinit(void);
