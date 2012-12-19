/*
 * USB keyboard/mouse constants
 */
enum {

	Stack = 32 * 1024,

	/* HID class subclass protocol ids */
	PtrCSP		= 0x020103,	/* mouse.boot.hid */
	KbdCSP		= 0x010103,	/* keyboard.boot.hid */

	/* Requests */
	Getreport = 0x01,
	Setreport = 0x09,
	Getproto	= 0x03,
	Setproto	= 0x0b,

	/* protocols for SET_PROTO request */
	Bootproto	= 0,
	Reportproto	= 1,

	/* protocols for SET_REPORT request */
	Reportout = 0x0200,
};

/*
 * USB HID report descriptor item tags
 */ 
enum {
	/* main items */
	Input	= 8,
	Output,
	Collection,
	Feature,

	CollectionEnd,

	/* global items */
	UsagPg = 0,
	LogiMin,
	LogiMax,
	PhysMin,
	PhysMax,
	UnitExp,
	UnitTyp,
	RepSize,
	RepId,
	RepCnt,

	Push,
	Pop,

	/* local items */
	Usage	= 0,
	UsagMin,
	UsagMax,
	DesgIdx,
	DesgMin,
	DesgMax,
	StrgIdx,
	StrgMin,
	StrgMax,

	Delim,
};

/* main item flags */
enum {
	Fdata	= 0<<0,	Fconst	= 1<<0,
	Farray	= 0<<1,	Fvar	= 1<<1,
	Fabs	= 0<<2,	Frel	= 1<<2,
	Fnowrap	= 0<<3,	Fwrap	= 1<<3,
	Flinear	= 0<<4,	Fnonlin	= 1<<4,
	Fpref	= 0<<5,	Fnopref	= 1<<5,
	Fnonull	= 0<<6,	Fnullst	= 1<<6,
};

enum {
	/* keyboard modifier bits */
	Mlctrl		= 0,
	Mlshift		= 1,
	Mlalt		= 2,
	Mlgui		= 3,
	Mrctrl		= 4,
	Mrshift		= 5,
	Mralt		= 6,
	Mrgui		= 7,

	/* masks for byte[0] */
	Mctrl		= 1<<Mlctrl | 1<<Mrctrl,
	Mshift		= 1<<Mlshift | 1<<Mrshift,
	Malt		= 1<<Mlalt | 1<<Mralt,
	Mcompose	= 1<<Mlalt,
	Maltgr		= 1<<Mralt,
	Mgui		= 1<<Mlgui | 1<<Mrgui,

	MaxAcc = 3,			/* max. ptr acceleration */
	PtrMask= 0xf,			/* 4 buttons: should allow for more. */

};

/*
 * Plan 9 keyboard driver constants.
 */
enum {
	/* Scan codes (see kbd.c) */
	SCesc1		= 0xe0,		/* first of a 2-character sequence */
	SCesc2		= 0xe1,
	SClshift		= 0x2a,
	SCrshift		= 0x36,
	SCctrl		= 0x1d,
	SCcompose	= 0x38,
	Keyup		= 0x80,		/* flag bit */
	Keymask		= 0x7f,		/* regular scan code bits */
};
