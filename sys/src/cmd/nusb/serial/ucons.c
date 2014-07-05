#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include "usb.h"
#include "serial.h"

enum {
	Net20DCVid	= 0x0525,	/* Ajays usb debug cable */
	Net20DCDid	= 0x127a,

	HuaweiVid	= 0x12d1,
	HuaweiE220	= 0x1003,
};

Cinfo uconsinfo[] = {
	{ Net20DCVid,	Net20DCDid },
	{ HuaweiVid,	HuaweiE220 },
	{ 0,		0 },
};

int
uconsmatch(Serial *ser, char *info)
{
	Cinfo *ip;
	char buf[50];

	for(ip = uconsinfo; ip->vid != 0; ip++){
		snprint(buf, sizeof buf, "vid %#06x did %#06x",
			ip->vid, ip->did);
		dsprint(2, "serial: %s %s\n", buf, info);
		if(strstr(info, buf) != nil){
			if(ip->vid == HuaweiVid && ip->did == HuaweiE220)
				ser->nifcs = 2;
			return 0;
		}
	}
	return -1;
}
