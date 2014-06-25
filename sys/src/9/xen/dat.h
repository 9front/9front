#include "../pc/dat.h"

typedef unsigned char uint8_t;
typedef unsigned char uint8;
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;
typedef unsigned long long uint64_t;
typedef char int8_t;
typedef short int16_t;
typedef long int32_t;
typedef long long int64_t;

#define __attribute__(x)	
enum {
	EINVAL,
	EACCES,
	EEXIST,
	EISDIR,
	ENOENT,
	ENOMEM,
	ENOSPC,
	EIO,
	ENOTEMPTY,
	ENOSYS,
	EROFS,
	EBUSY,
	EAGAIN,
	EISCONN,
};

#include "xendat.h"

#undef mk_unsigned_long
#define mk_unsigned_long(x) ((unsigned long)(x))

#ifndef set_xen_guest_handle
#define set_xen_guest_handle(hnd, val)	hnd = val
#endif

extern ulong hypervisor_virt_start;
extern ulong *patomfn, *matopfn;
extern start_info_t *xenstart;
extern ulong xentop;
extern shared_info_t *HYPERVISOR_shared_info;

/*
 * Fake kmap
 * XXX is this still viable?
 */
#undef VA
#define	VA(k)		((ulong)(k))
#define	kmap(p)		(KMap*)((p)->pa|KZERO)
#define	kunmap(k)
