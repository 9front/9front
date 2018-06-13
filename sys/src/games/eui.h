enum{
	MILLION = 1000000,
	BILLION = 1000000000,
};

extern u64int keys, keys2;
extern int trace, paused;
extern int savereq, loadreq;
extern QLock pauselock;
extern int scale, fixscale, warp10;
extern uchar *pic;

void*	emalloc(ulong);
void	flushmouse(int);
void	flushscreen(void);
void	flushaudio(int(*)(void));
void	regkeyfn(Rune, void(*)(void));
void	regkey(char*, Rune, int);
void	initemu(int, int, int, ulong, int, void(*)(void*));
