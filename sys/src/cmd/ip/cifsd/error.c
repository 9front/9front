#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

enum {
	/* error class */
	ERRDOS = 1,
	ERRSRV = 2,
	ERRHRD = 3,
	ERRCMD = 0xFF,

	/* error codes */
	ERRbadfunc = 0x1,
	ERRbadfile = 0x2,
	ERRbadpath = 0x3,
	ERRnofids = 0x4,
	ERRnoaccess = 0x5,
	ERRbadfid = 0x6,
	ERRbadmcp = 0x7,
	ERRnomem = 0x8,
	ERRbadmem = 0x9,
	ERRbadenv = 0xA,
	ERRbadformat = 0xB,
	ERRbadaccess = 0xC,
	ERRbaddata = 0xD,
	ERRbaddrive = 0xF,
	ERRremcd = 0x10,
	ERRdiffdevice = 0x11,
	ERRnofiles = 0x12,
	ERRgeneral = 0x1F,
	ERRbadshare = 0x20,
	ERRlock = 0x21,
	ERReof = 0x26,
	ERRunsup = 0x32,
	ERRfilexists = 0x50,
	ERRinvalidparam = 0x57,
	ERRunknownlevel = 0x7C,
	ERRbadpipe = 0xE6,
	ERRinvnetname = 0x06,
	ERRreqnotaccep = 0x47,
	ERRnosuchshare = 0x43,
	ERRerror = 0x1,
	ERRbadpw = 0x2,
	ERRaccess = 0x4,
	ERRinvtid = 0x5,
	ERRinvdevice = 0x7,
	ERRbaduid = 0x5b,
};

int
doserror(int err)
{
#define SE(c,e)	e<<16 | c
	static struct Ent {
		int error;
		int status;
	} tab[] = {
		SE(ERRSRV, ERRerror), STATUS_INVALID_SMB,
		SE(ERRSRV, ERRinvtid), STATUS_SMB_BAD_TID,
		SE(ERRDOS, ERRbadfid), STATUS_SMB_BAD_FID,
		SE(ERRDOS, ERRbadaccess), STATUS_OS2_INVALID_ACCESS,
		SE(ERRSRV, ERRbaduid), STATUS_SMB_BAD_UID,
		SE(ERRDOS, ERRunknownlevel), STATUS_OS2_INVALID_LEVEL,
		SE(ERRDOS, ERRnofiles), STATUS_NO_MORE_FILES,
		SE(ERRDOS, ERRbadfid), STATUS_INVALID_HANDLE,
		SE(ERRDOS, ERRnoaccess), STATUS_ACCESS_DENIED,
		SE(ERRDOS, ERRbadfile), STATUS_OBJECT_NAME_NOT_FOUND,
		SE(ERRDOS, ERRfilexists), STATUS_OBJECT_NAME_COLLISION,
		SE(ERRDOS, ERRbadpath), STATUS_OBJECT_PATH_INVALID,
		SE(ERRDOS, ERRbadpath), STATUS_OBJECT_PATH_NOT_FOUND,
		SE(ERRDOS, ERRbadpath), STATUS_OBJECT_PATH_SYNTAX_BAD,
		SE(ERRDOS, ERRbadshare), STATUS_SHARING_VIOLATION,
		SE(ERRSRV, ERRbadpw), STATUS_LOGON_FAILURE,
		SE(ERRDOS, ERRnoaccess), STATUS_FILE_IS_A_DIRECTORY,
		SE(ERRDOS, ERRunsup), STATUS_NOT_SUPPORTED,
		SE(ERRSRV, ERRinvdevice), STATUS_BAD_DEVICE_TYPE,
		SE(ERRSRV, ERRinvnetname), STATUS_BAD_NETWORK_NAME,
		SE(ERRDOS, ERRdiffdevice), STATUS_NOT_SAME_DEVICE,
		SE(ERRDOS, ERRremcd), STATUS_DIRECTORY_NOT_EMPTY,
		SE(ERRSRV, ERRerror), 0,
	};
	struct Ent *p;

	for(p=tab; p->status; p++)
		if(p->status == err)
			break;
	return p->error;
}

int
smbmkerror(void)
{
	static struct Ent {
		int status;
		char *str;
	}  tab[] = {
		STATUS_ACCESS_DENIED, "permission denied",
		STATUS_ACCESS_DENIED, "access permission denied",
		STATUS_ACCESS_DENIED, "create prohibited",
		STATUS_ACCESS_DENIED, "mounted directory forbids creation",
		STATUS_DIRECTORY_NOT_EMPTY, "directory not empty",
		STATUS_NO_SUCH_FILE, "no such file",
		STATUS_OBJECT_NAME_NOT_FOUND, "name not found",
		STATUS_OBJECT_PATH_NOT_FOUND, "directory entry not found",
		STATUS_OBJECT_PATH_NOT_FOUND, "not a directory",
		STATUS_OBJECT_PATH_NOT_FOUND, "does not exist",
		STATUS_OBJECT_PATH_SYNTAX_BAD, "bad character",
		STATUS_OBJECT_PATH_SYNTAX_BAD, "file name syntax",
		STATUS_OBJECT_NAME_COLLISION, "file already exists",
		STATUS_FILE_IS_A_DIRECTORY, "is a directory",
		/* kenfs */
		STATUS_OBJECT_NAME_COLLISION, "create/wstat -- file exists",
		STATUS_ACCESS_DENIED, "wstat -- not owner",
		STATUS_ACCESS_DENIED, "wstat -- not in group",
		/* unknown error */
		STATUS_INVALID_SMB, nil,
	};
	char buf[ERRMAX];
	struct Ent *p;

	rerrstr(buf, sizeof(buf));
	for(p = tab; p->str; p++)
		if(strstr(buf, p->str))
			break;
	if(debug)
		fprint(2, "smbmkerror: %s -> %lux\n", buf, (ulong)p->status);
	return p->status;
}
