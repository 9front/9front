#ifndef _STDINT_H_
#define _STDINT_H_ 1

typedef int _intptr_t;
typedef unsigned int _uintptr_t;

typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef _intptr_t intptr_t;
typedef _uintptr_t uintptr_t;

#define INT8_MIN	0x80
#define INT16_MIN	0x8000
#define INT32_MIN	0x80000000
#define INT64_MIN	0x8000000000000000LL

#define INT8_MAX	0x7f
#define INT16_MAX	0x7fff
#define INT32_MAX	0x7fffffff
#define INT64_MAX	0x7fffffffffffffffULL

#define UINT8_MAX	0xff
#define UINT16_MAX	0xffff
#define UINT32_MAX	0xffffffffL
#define UINT64_MAX	0xffffffffffffffffULL

#endif
