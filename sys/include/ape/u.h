#ifndef __U_H
#define __U_H
#ifndef _PLAN9_SOURCE
   This header file is an extension to ANSI/POSIX
#endif

#define nil		((void*)0)
typedef	unsigned short	ushort;
typedef	unsigned char	uchar;
typedef unsigned long	ulong;
typedef unsigned int	uint;
typedef signed char	schar;
typedef	long long	vlong;
typedef	unsigned long long uvlong;
typedef	uint		Rune;
typedef 	union FPdbleword FPdbleword;
typedef	char*	p9va_list;

typedef uchar	u8int;
typedef ushort	u16int;
typedef ulong	u32int;
typedef uvlong	u64int;
typedef signed char	s8int;
typedef signed short	s16int;
typedef signed int	s32int;
typedef signed long long	s64int;

#endif
