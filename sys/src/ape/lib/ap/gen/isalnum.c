#include <ctype.h>

#undef isalnum
#undef isalpha
#undef isblank
#undef iscntrl
#undef isdigit
#undef isgraph
#undef islower
#undef isprint
#undef ispunct
#undef isspace
#undef isupper
#undef isxdigit

int isalnum(int c) {return _ctype[(unsigned char)(c)]&(_ISupper|_ISlower|_ISdigit);}
int isalpha(int c) {return _ctype[(unsigned char)(c)]&(_ISupper|_ISlower);}
int isblank(int c) {return _ctype[(unsigned char)(c)]&_ISblank;}
int iscntrl(int c) {return _ctype[(unsigned char)(c)]&_IScntrl;}
int isdigit(int c) {return _ctype[(unsigned char)(c)]&_ISdigit;}
int isgraph(int c) {return _ctype[(unsigned char)(c)]&(_ISpunct|_ISupper|_ISlower|_ISdigit);}
int islower(int c) {return _ctype[(unsigned char)(c)]&_ISlower;}
int isprint(int c) {return _ctype[(unsigned char)(c)]&(_ISpunct|_ISupper|_ISlower|_ISdigit|_ISblank);}
int ispunct(int c) {return _ctype[(unsigned char)(c)]&_ISpunct;}
int isspace(int c) {return _ctype[(unsigned char)(c)]&_ISspace;}
int isupper(int c) {return _ctype[(unsigned char)(c)]&_ISupper;}
int isxdigit(int c) {return _ctype[(unsigned char)(c)]&_ISxdigit;}
