typedef struct Cursor Cursor;
typedef struct Cursorinfo Cursorinfo;
struct Cursorinfo {
	Cursor;
	Lock;
};

extern Cursorinfo	cursor;
extern Cursor		arrow;
extern Memimage		*gscreen;
extern int		cursorver;
extern Point		cursorpos;

void		mouseresize(void);
Point 		mousexy(void);
void		cursoron(void);
void		cursoroff(void);
void		setcursor(Cursor*);
void		flushmemscreen(Rectangle r);
Rectangle	cursorrect(void);
void		cursordraw(Memimage *dst, Rectangle r);

extern QLock	drawlock;
void		drawactive(int);
void		getcolor(ulong, ulong*, ulong*, ulong*);
int		setcolor(ulong, ulong, ulong, ulong);
#define		TK2SEC(x)	0
extern void	blankscreen(int);
void		screeninit(int x, int y, char *chanstr);
void		screenwin(void);
void		absmousetrack(int x, int y, int b, ulong msec);
Memdata*	attachscreen(Rectangle*, ulong*, int*, int*, int*);
void		deletescreenimage(void);
void		resetscreenimage(void);

void		fsinit(char *mntpt, int x, int y, char *chanstr);
#define		ishwimage(i)	0
