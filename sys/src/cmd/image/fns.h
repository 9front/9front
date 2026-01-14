void initworkrects(Rectangle*, int, Rectangle*);
void *emalloc(ulong);
void *erealloc(void*, ulong);
Memimage *eallocmemimage(Rectangle, ulong);
Memimage *ereadmemimage(int);
int ewritememimage(int, Memimage*);
Image *eallocimage(Display*, Rectangle, ulong, int, ulong);
Image *memimage2image(Display*, Memimage*);
