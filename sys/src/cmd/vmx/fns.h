#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
void *emalloc(ulong);
void loadkernel(char *);
uvlong rget(char *);
void rpoke(char *, uvlong, int);
#define rset(a,b) rpoke(a,b,0)
void rsetsz(char *, uvlong, int);
uvlong rgetsz(char *, int);
void processexit(char *);
void pitadvance(void);
void rtcadvance(void);
void settimer(vlong targ);
void vmerror(char *, ...);
#pragma varargck argpos vmerror 1
#define vmdebug if(!debug) {} else vmerror
int ctl(char *, ...);
void registermmio(uvlong, uvlong, uvlong (*)(int, uvlong, uvlong));
void irqline(int, int);
void irqack(int);
void postexc(char *, vlong);
void vgaresize(void);
void uartinit(int, char *);
void sendnotif(void (*)(void *), void *);
PCIDev *mkpcidev(u32int, u32int, u32int, int);
PCIBar *mkpcibar(PCIDev *, u8int, u32int, u32int, void *, void *);
PCICap *mkpcicap(PCIDev *, u8int, u32int (*)(PCICap *, u8int), void(*)(PCICap *, u8int, u32int, u32int));
u32int allocbdf(void);
Region *regptr(u64int);
void *gptr(u64int, u64int);
void *gend(void *);
uintptr gpa(void *);
uintptr gavail(void *);
void pciirq(PCIDev *, int);
u32int iowhine(int, u16int, u32int, int, void *);
void elcr(u16int);
int mkvionet(char *);
int mkvioblk(char *);
int mkideblk(char *);
char* rcflush(int);
void i8042kick(void *);
#define GET8(p,n) (*((u8int*)(p)+(n)))
#define GET16(p,n) (*(u16int*)((u8int*)(p)+(n)))
#define GET32(p,n) (*(u32int*)((u8int*)(p)+(n)))
#define GET64(p,n) (*(u64int*)((u8int*)(p)+(n)))
#define PUT8(p,n,v) (*((u8int*)(p)+(n)) = (v))
#define PUT16(p,n,v) (*(u16int*)((u8int*)(p)+(n)) = (v))
#define PUT32(p,n,v) (*(u32int*)((u8int*)(p)+(n)) = (v))
#define PUT64(p,n,v) (*(u64int*)((u8int*)(p)+(n)) = (v))
u32int roundpow2(u32int);
u32int vgagetpal(u8int);
void vgasetpal(u8int, u32int);
uintptr vmemread(void *, uintptr, uintptr);
uintptr vmemwrite(void *, uintptr, uintptr);
int x86access(int, uintptr, int, uvlong*, int, int, TLB *);
u32int io(int, u16int, u32int, int);
int x86step(void);
uvlong nanosec(void);
