#include <u.h>
#include <libc.h>
#include <avl.h>
#include <fcall.h>
#include "dat.h"

char Efs[]	= "internal error";
char Enoval[]	= "message to missing key";
char Ecorrupt[] = "block contents corrupted";
char Efsvers[]	= "unknown fs version";
char Eimpl[]	= "not implemented";
char Ebotch[]	= "protocol botch";
char Eio[]	= "i/o error";
char Enofid[]	= "unknown fid";
char Efid[]	= "fid in use";
char Etype[]	= "invalid fid type";
char Edscan[]	= "invalid dir scan offset";
char Esrch[]	= "directory entry not found";
char Eexist[]	= "create/wstat -- file exists";
char Emode[]	= "open/create -- unknown mode";
char Efull[]	= "file system full";
char Estuffed[]	= "emergency blocks exhausted";
char Eauth[]	= "authentication failed";
char Elength[]	= "name too long";
char Eperm[]	= "permission denied";
char Einuse[]	= "resource in use";
char Ebadf[]	= "invalid file";
char Ename[]	= "create/wstat -- bad character in file name";
char Enomem[]	= "out of memory";
char Eattach[]	= "attach required";
char Enosnap[]	= "attach -- bad specifier";
char Edir[]	= "invalid directory";
char Esyntax[]	= "syntax error";
char Enouser[]	= "user does not exist";
char Enogrp[]	= "group does not exist";
char Efsize[]	= "file too big";
char Ebadu[]	= "attach -- unknown user or failed authentication";
char Erdonly[]	= "file system read only";
char Elocked[]	= "open/create -- file is locked";
char Eauthp[]	= "authread -- auth protocol not finished";
char Eauthd[]	= "authread -- not enough data";
char Eauthph[]	= "auth phase error";
char Enone[]	= "auth -- user 'none' requires no authentication";
char Enoauth[]	= "auth -- authentication disabled";
char Ephase[]	= "phase error -- use after remove";
char Ecdir[]	= "create -- in a non-directory";
char Ebadctl[]	= "invalid control message";
char Enoqid[]	= "qids exhausted";
char Enempty[]	= "directory is not empty";
char Enoadm[]	= "missing adm snapshot";
char Echeck[]	= "check -- fs not ok";
char Eopen[]	= "read/write -- on non open fid";
char Eoffset[]	= "read/write -- offset negative";

char Esnapu[]	= "snap -- is currently mounted";
char Esnapx[]	= "snap -- already exists";
char Esnapr[]	= "snap -- reserved name";

char Ewstatb[]	= "wstat -- unknown bits in qid.type/mode";
char Ewstatd[]	= "wstat -- attempt to change directory";
char Ewstatg[]	= "wstat -- not in group";
char Ewstatl[]	= "wstat -- attempt to make length negative";
char Ewstatm[]	= "wstat -- attempt to change muid";
char Ewstato[]	= "wstat -- not owner or group leader";
char Ewstatu[]	= "wstat -- not owner";
char Ewstatq[]	= "wstat -- attempt to change qid";
