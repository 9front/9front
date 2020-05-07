#ifndef _STDINT_GENERIC_H_
#define _STDINT_GENERIC_H_ 1

/*
 * Default for 32 bit architectures, overriden by
 * /$objtype/include/ape/stdint.h if needed.
 */
#ifndef _STDINT_ARCH_H_
typedef int _intptr_t;
typedef unsigned int _uintptr_t;
#endif

typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
typedef long long intmax_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned long long uintmax_t;

typedef int8_t	int_fast8_t;
typedef int16_t	int_fast16_t;
typedef int32_t	int_fast32_t;
typedef int64_t	int_fast64_t;
;
typedef int8_t	int_least8_t;
typedef int16_t	int_least16_t;
typedef int32_t	int_least32_t;
typedef int64_t	int_least64_t;

typedef uint8_t		uint_fast8_t;
typedef uint16_t	uint_fast16_t;
typedef uint32_t	uint_fast32_t;
typedef uint64_t	uint_fast64_t;

typedef uint8_t		uint_least8_t;
typedef uint16_t	uint_least16_t;
typedef uint32_t	uint_least32_t;
typedef uint64_t	uint_least64_t;

typedef _intptr_t intptr_t;
typedef _uintptr_t uintptr_t;

#define INT8_MIN	((int8_t)0x80)
#define INT16_MIN	((int16_t)0x8000)
#define INT32_MIN	((int32_t)0x80000000)
#define INT64_MIN	((int64_t)0x8000000000000000LL)
#define INTMAX_MIN	INT64_MIN

#define UINT8_MIN	0
#define UINT16_MIN	0 
#define UINT32_MIN	0 
#define UINT64_MIN	0
#define UINTMAX_MIN	UINT64_MIN

#define INT_FAST8_MIN	INT8_MIN
#define INT_FAST16_MIN	INT16_MIN
#define INT_FAST32_MIN	INT32_MIN
#define INT_FAST64_MIN	INT64_MIN

#define UINT_FAST8_MIN	UINT8_MIN
#define UINT_FAST16_MIN	UINT16_MIN
#define UINT_FAST32_MIN	UINT32_MIN
#define UINT_FAST64_MIN	UINT64_MIN

#define INT_LEAST8_MIN	INT8_MIN
#define INT_LEAST16_MIN	INT16_MIN
#define INT_LEAST32_MIN	INT32_MIN
#define INT_LEAST64_MIN	INT64_MIN

#define UINT_LEAST8_MIN		UINT8_MIN
#define UINT_LEAST16_MIN	UINT16_MIN
#define UINT_LEAST32_MIN	UINT32_MIN
#define UINT_LEAST64_MIN	UINT64_MIN

#define INT8_MAX	0x7f
#define INT16_MAX	0x7fff
#define INT32_MAX	0x7fffffff
#define INT64_MAX	0x7fffffffffffffffLL
#define INTMAX_MAX	INT64_MAX

#define UINT8_MAX	0xff
#define UINT16_MAX	0xffff
#define UINT32_MAX	0xffffffffL
#define UINT64_MAX	0xffffffffffffffffULL
#define UINTMAX_MAX	UINT64_MAX

#define INT_FAST8_MAX	INT8_MAX
#define INT_FAST16_MAX	INT16_MAX
#define INT_FAST32_MAX	INT32_MAX
#define INT_FAST64_MAX	INT64_MAX

#define UINT_FAST8_MAX	UINT8_MAX
#define UINT_FAST16_MAX	UINT16_MAX
#define UINT_FAST32_MAX	UINT32_MAX
#define UINT_FAST64_MAX	UINT64_MAX

#define INT_LEAST8_MAX	INT8_MAX
#define INT_LEAST16_MAX	INT16_MAX
#define INT_LEAST32_MAX	INT32_MAX
#define INT_LEAST64_MAX	INT64_MAX

#define UINT_LEAST8_MAX		UINT8_MAX
#define UINT_LEAST16_MAX	UINT16_MAX
#define UINT_LEAST32_MAX	UINT32_MAX
#define UINT_LEAST64_MAX	UINT64_MAX

/* 
 * Right now, all of our size_t types are 32 bit, even on
 * 64 bit architectures.
 */
#define SIZE_MIN	UINT32_MIN
#define SIZE_MAX	UINT32_MAX

#endif
