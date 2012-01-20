/* handy strings in l.s */
extern char origin[];
extern char hex[];
extern char crnl[];
extern char bootname[];

/* l.s */
void start(void *sp);
int getc(void);
int gotc(void);
void putc(int c);
void usleep(int t);
void halt(void);
void jump(void *pc);

int read(void *f, void *data, int len);
int readn(void *f, void *data, int len);
void close(void *f);
void unload(void);

void memset(void *p, int v, int n);
void memmove(void *dst, void *src, int n);
int memcmp(void *src, void *dst, int n);
int strlen(char *s);
char *strchr(char *s, int c);
char *strrchr(char *s, int c);
void print(char *s);

char *configure(void *f, char *path);
char *bootkern(void *f);

/* a20.s */
int a20(void);

/* e820.s */
ulong e820(ulong bx, void *p);

/* apm.s */
void apm(int id);
