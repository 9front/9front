typedef struct Cursor Cursor;
typedef struct Cursorinfo Cursorinfo;
struct Cursorinfo {
	Cursor;
	Lock;
};

/* devmouse.c */
extern void mousetrack(int, int, int, ulong);
extern void absmousetrack(int, int, int, ulong);
extern Point mousexy(void);

extern void mouseaccelerate(int);
extern int m3mouseputc(Queue*, int);
extern int m5mouseputc(Queue*, int);
extern int mouseputc(Queue*, int);

extern Cursorinfo cursor;
extern Cursor arrow;

/* mouse.c */
extern void mousectl(Cmdbuf*);
extern void mouseresize(void);
extern void mouseredraw(void);

/* screen.c */
extern void	blankscreen(int);
extern void	flushmemscreen(Rectangle);
extern uchar*	attachscreen(Rectangle*, ulong*, int*, int*, int*);
extern void	cursoron(void);
extern void	cursoroff(void);
extern void	setcursor(Cursor*);

/* devdraw.c */
extern QLock	drawlock;

#define ishwimage(i)	0		/* for ../port/devdraw.c */

/* swcursor.c */
void		swcursorhide(void);
void		swcursoravoid(Rectangle);
void		swcursordraw(Point);
void		swcursorload(Cursor *);
void		swcursorinit(void);
