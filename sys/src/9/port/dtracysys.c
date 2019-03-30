#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include "/sys/src/libc/9syscall/sys.h"

#include	<dtracy.h>
#include	<ctype.h>

static DTProbe **dtpsysentry, **dtpsysreturn;

typedef uintptr Syscall(va_list);
extern Syscall *systab[];

#define WRAP0(x,y,z)\
	Syscall z; uintptr x(va_list va){\
	uintptr rc;\
	DTTrigInfo info;\
	memset(&info, 0, sizeof(info));\
	dtptrigger(dtpsysentry[y], &info);\
	rc = z(va);\
	info.arg[9] = (uvlong) rc;\
	dtptrigger(dtpsysreturn[y], &info);\
	return rc;\
}
#define WRAP1(x,y,z,type0)\
	Syscall z; uintptr x(va_list va){\
	uintptr rc;\
	va_list vb = va;\
	DTTrigInfo info;\
	memset(&info, 0, sizeof(info));\
	info.arg[0] = (uvlong) va_arg(vb, type0);\
	dtptrigger(dtpsysentry[y], &info);\
	rc = z(va);\
	info.arg[9] = (uvlong) rc;\
	dtptrigger(dtpsysreturn[y], &info);\
	return rc;\
}
#define WRAP2(x,y,z,type0,type1)\
	Syscall z; uintptr x(va_list va){\
	uintptr rc;\
	va_list vb = va;\
	DTTrigInfo info;\
	memset(&info, 0, sizeof(info));\
	info.arg[0] = (uvlong) va_arg(vb, type0);\
	info.arg[1] = (uvlong) va_arg(vb, type1);\
	dtptrigger(dtpsysentry[y], &info);\
	rc = z(va);\
	info.arg[9] = (uvlong) rc;\
	dtptrigger(dtpsysreturn[y], &info);\
	return rc;\
}
#define WRAP3(x,y,z,type0,type1,type2)\
	Syscall z; uintptr x(va_list va){\
	uintptr rc;\
	va_list vb = va;\
	DTTrigInfo info;\
	memset(&info, 0, sizeof(info));\
	info.arg[0] = (uvlong) va_arg(vb, type0);\
	info.arg[1] = (uvlong) va_arg(vb, type1);\
	info.arg[2] = (uvlong) va_arg(vb, type2);\
	dtptrigger(dtpsysentry[y], &info);\
	rc = z(va);\
	info.arg[9] = (uvlong) rc;\
	dtptrigger(dtpsysreturn[y], &info);\
	return rc;\
}
#define WRAP4(x,y,z,type0,type1,type2,type3)\
	Syscall z; uintptr x(va_list va){\
	uintptr rc;\
	va_list vb = va;\
	DTTrigInfo info;\
	memset(&info, 0, sizeof(info));\
	info.arg[0] = (uvlong) va_arg(vb, type0);\
	info.arg[1] = (uvlong) va_arg(vb, type1);\
	info.arg[2] = (uvlong) va_arg(vb, type2);\
	info.arg[3] = (uvlong) va_arg(vb, type3);\
	dtptrigger(dtpsysentry[y], &info);\
	rc = z(va);\
	info.arg[9] = (uvlong) rc;\
	dtptrigger(dtpsysreturn[y], &info);\
	return rc;\
}
/*TODO*/
#define WRAP5(x,y,z,type0,type1,type2,type3,type4)\
	Syscall z; uintptr x(va_list va){\
	uintptr rc;\
	va_list vb = va;\
	DTTrigInfo info;\
	memset(&info, 0, sizeof(info));\
	info.arg[0] = (uvlong) va_arg(vb, type0);\
	info.arg[1] = (uvlong) va_arg(vb, type1);\
	info.arg[2] = (uvlong) va_arg(vb, type2);\
	info.arg[3] = (uvlong) va_arg(vb, type3);\
	info.arg[4] = (uvlong) va_arg(vb, type4);\
	dtptrigger(dtpsysentry[y], &info);\
	rc = z(va);\
	info.arg[9] = (uvlong) rc;\
	dtptrigger(dtpsysreturn[y], &info);\
	return rc;\
}

WRAP0(dtwrap_sysr1, SYSR1, sysr1)
WRAP1(dtwrap_sys_errstr, _ERRSTR, sys_errstr, char*)
WRAP3(dtwrap_sysbind, BIND, sysbind, char*, char*, int)
WRAP1(dtwrap_syschdir, CHDIR, syschdir, char*)
WRAP1(dtwrap_sysclose, CLOSE, sysclose, int)
WRAP2(dtwrap_sysdup, DUP, sysdup, int, int)
WRAP1(dtwrap_sysalarm, ALARM, sysalarm, ulong)
WRAP2(dtwrap_sysexec, EXEC, sysexec, char *, char **)
WRAP1(dtwrap_sysexits, EXITS, sysexits, char *)
WRAP3(dtwrap_sys_fsession, _FSESSION, sys_fsession, int, char *, uint)
WRAP2(dtwrap_sysfauth, FAUTH, sysfauth, int, char *)
WRAP2(dtwrap_sys_fstat, _FSTAT, sys_fstat, int, uchar *)
WRAP1(dtwrap_syssegbrk, SEGBRK, syssegbrk, void *)
WRAP4(dtwrap_sys_mount, _MOUNT, sys_mount, int, char *, int, char *)
WRAP2(dtwrap_sysopen, OPEN, sysopen, char *, int)
WRAP3(dtwrap_sys_read, _READ, sys_read, int, void*, long)
WRAP3(dtwrap_sysoseek, OSEEK, sysoseek, int, long, int)
WRAP1(dtwrap_syssleep, SLEEP, syssleep, long)
WRAP2(dtwrap_sys_stat, _STAT, sys_stat, char *, uchar *)
WRAP1(dtwrap_sysrfork, RFORK, sysrfork, int)
WRAP3(dtwrap_sys_write, _WRITE, sys_write, int, void *, long)
WRAP1(dtwrap_syspipe, PIPE, syspipe, int*)
WRAP3(dtwrap_syscreate, CREATE, syscreate, char*, int, int)
WRAP3(dtwrap_sysfd2path, FD2PATH, sysfd2path, int, char*, uint)
WRAP1(dtwrap_sysbrk_, BRK_, sysbrk_, uintptr)
WRAP1(dtwrap_sysremove, REMOVE, sysremove, char *)
WRAP0(dtwrap_sys_wstat, _WSTAT, sys_wstat)
WRAP0(dtwrap_sys_fwstat, _FWSTAT, sys_fwstat)
WRAP2(dtwrap_sysnotify, NOTIFY, sysnotify, char *, void *)
WRAP1(dtwrap_sysnoted, NOTED, sysnoted, int)
WRAP4(dtwrap_syssegattach, SEGATTACH, syssegattach, int, char *, uintptr, ulong)
WRAP1(dtwrap_syssegdetach, SEGDETACH, syssegdetach, uintptr)
WRAP2(dtwrap_syssegfree, SEGFREE, syssegfree, uintptr, ulong)
WRAP2(dtwrap_syssegflush, SEGFLUSH, syssegflush, void*, ulong)
WRAP2(dtwrap_sysrendezvous, RENDEZVOUS, sysrendezvous, uintptr, uintptr)
WRAP2(dtwrap_sysunmount, UNMOUNT, sysunmount, char *, char *)
WRAP1(dtwrap_sys_wait, _WAIT, sys_wait, void*)
WRAP2(dtwrap_syssemacquire, SEMACQUIRE, syssemacquire, long*, int)
WRAP2(dtwrap_syssemrelease, SEMRELEASE, syssemrelease, long*, long)
WRAP4(dtwrap_sysfversion, FVERSION, sysfversion, int, int, char *, int)
WRAP2(dtwrap_syserrstr, ERRSTR, syserrstr, char *, uint)
WRAP3(dtwrap_sysstat, STAT, sysstat, char *, uchar *, uint)
WRAP3(dtwrap_sysfstat, FSTAT, sysfstat, int, uchar *, uint)
WRAP3(dtwrap_syswstat, WSTAT, syswstat, char *, uchar *, uint)
WRAP3(dtwrap_sysfwstat, FWSTAT, sysfwstat, int, uchar *, uint)
WRAP5(dtwrap_sysmount, MOUNT, sysmount, int, int, char *, int, char *)
WRAP2(dtwrap_sysawait, AWAIT, sysawait, char *, uint)
WRAP4(dtwrap_syspread, PREAD, syspread, int, void *, long, vlong)
WRAP4(dtwrap_syspwrite, PWRITE, syspwrite, int, void *, long, vlong)
WRAP2(dtwrap_systsemacquire, TSEMACQUIRE, systsemacquire, long *, ulong)


/* TODO: amd64 */
WRAP4(dtwrap_sysseek, SEEK, sysseek, vlong*, int, vlong, int)
WRAP1(dtwrap_sys_nsec, _NSEC, sys_nsec, vlong*)

static Syscall *wraptab[]={
	[SYSR1]		dtwrap_sysr1,
	[_ERRSTR]	dtwrap_sys_errstr,
	[BIND]		dtwrap_sysbind,
	[CHDIR]		dtwrap_syschdir,
	[CLOSE]		dtwrap_sysclose,
	[DUP]		dtwrap_sysdup,
	[ALARM]		dtwrap_sysalarm,
	[EXEC]		dtwrap_sysexec,
	[EXITS]		dtwrap_sysexits,
	[_FSESSION]	dtwrap_sys_fsession,
	[FAUTH]		dtwrap_sysfauth,
	[_FSTAT]	dtwrap_sys_fstat,
	[SEGBRK]	dtwrap_syssegbrk,
	[_MOUNT]	dtwrap_sys_mount,
	[OPEN]		dtwrap_sysopen,
	[_READ]		dtwrap_sys_read,
	[OSEEK]		dtwrap_sysoseek,
	[SLEEP]		dtwrap_syssleep,
	[_STAT]		dtwrap_sys_stat,
	[RFORK]		dtwrap_sysrfork,
	[_WRITE]	dtwrap_sys_write,
	[PIPE]		dtwrap_syspipe,
	[CREATE]	dtwrap_syscreate,
	[FD2PATH]	dtwrap_sysfd2path,
	[BRK_]		dtwrap_sysbrk_,
	[REMOVE]	dtwrap_sysremove,
	[_WSTAT]	dtwrap_sys_wstat,
	[_FWSTAT]	dtwrap_sys_fwstat,
	[NOTIFY]	dtwrap_sysnotify,
	[NOTED]		dtwrap_sysnoted,
	[SEGATTACH]	dtwrap_syssegattach,
	[SEGDETACH]	dtwrap_syssegdetach,
	[SEGFREE]	dtwrap_syssegfree,
	[SEGFLUSH]	dtwrap_syssegflush,
	[RENDEZVOUS]	dtwrap_sysrendezvous,
	[UNMOUNT]	dtwrap_sysunmount,
	[_WAIT]		dtwrap_sys_wait,
	[SEMACQUIRE]	dtwrap_syssemacquire,
	[SEMRELEASE]	dtwrap_syssemrelease,
	[SEEK]		dtwrap_sysseek,
	[FVERSION]	dtwrap_sysfversion,
	[ERRSTR]	dtwrap_syserrstr,
	[STAT]		dtwrap_sysstat,
	[FSTAT]		dtwrap_sysfstat,
	[WSTAT]		dtwrap_syswstat,
	[FWSTAT]	dtwrap_sysfwstat,
	[MOUNT]		dtwrap_sysmount,
	[AWAIT]		dtwrap_sysawait,
	[PREAD]		dtwrap_syspread,
	[PWRITE]	dtwrap_syspwrite,
	[TSEMACQUIRE]	dtwrap_systsemacquire,
	[_NSEC]		dtwrap_sys_nsec,
};

static void
sysprovide(DTProvider *prov)
{
	char buf[32], pname[32];
	int i;
	
	dtpsysentry = smalloc(sizeof(Syscall *) * nsyscall);
	dtpsysreturn = smalloc(sizeof(Syscall *) * nsyscall);
	for(i = 0; i < nsyscall; i++){
		if(systab[i] == nil || sysctab[i] == nil) continue;
		strecpy(buf, buf + sizeof(buf), sysctab[i]);
		if(isupper(buf[0])) buf[0] += 'a' - 'A';
		if(i == SYSR1) strcpy(buf, "r1");
		snprint(pname, sizeof(pname), "sys:%s:entry", buf);
		dtpsysentry[i] = dtpnew(pname, prov, (void *) i);
		snprint(pname, sizeof(pname), "sys:%s:return", buf);
		dtpsysreturn[i] = dtpnew(pname, prov, (void *) i);
	}
}

static int
sysenable(DTProbe *p)
{
	int i;
	Syscall *z;
	
	i = (int)(uintptr)p->aux;
	assert(i >= 0 && i < nsyscall);
	if(dtpsysentry[i]->nenable + dtpsysreturn[i]->nenable == 0)
		z = systab[i], systab[i] = wraptab[i], wraptab[i] = z;
	return 0;
}

static void
sysdisable(DTProbe *p)
{
	int i;
	Syscall *z;
	
	i = (int)(uintptr)p->aux;
	assert(i >= 0 && i < nsyscall);
	if(dtpsysentry[i]->nenable + dtpsysreturn[i]->nenable == 0)
		z = systab[i], systab[i] = wraptab[i], wraptab[i] = z;
}

DTProvider dtracysysprov = {
	.name = "sys",
	.provide = sysprovide,
	.enable = sysenable,
	.disable = sysdisable,
};
