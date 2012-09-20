Txtab regtx[] = {
	Ftype,	"type",	0,
	Fflags,	"flags",	0,
	Fcmd,	"cmd",	0,
	Ffeat,	"feat",	0,
	Flba0,	"lba0",	0,
	Flba8,	"lba8",	0,
	Flba16,	"lba16",	0,
	Fdev,	"dev",	0,
	Flba24,	"lba24",	0,
	Flba32,	"lba32",	0,
	Flba40,	"lba40",	0,
	Ffeat8,	"feat8",	0,
	Fsc,	"sc",	0,
	Fsc8,	"sc8",	0,
	Ficc,	"icc",	0,
	Fcontrol,"control",	0,

	/* aliases */
	Ffeat,	"features",	0,
	Flba0,	"sector",	0,
	Flba8,	"cyl0",	0,
	Flba8,	"byte0",	0,
	Flba16,	"cyl8",	0,
	Flba24,	"dh",	0,
	Flba24,	"byte8",	0,
	Flba32,	"cyl24",	0,
	Flba40,	"cyl32",	0,
};

Txtab smautosave[] = {
	0,	"disable",	0,
	0xf1,	"enable",		0,
};

Fetab _b0d2[] = {
	Fsc,	smautosave,	nelem(smautosave),
	0,	0,		0,
};

Txtab smlba8[] = {
	0x4f,	"",		0,
};
Txtab smlba16[] = {
	0xc2,	"",		0,
};

Txtab smartfeat[] = {
//	0xd0,	"read data",			0,
	0xd2,	"attribute autosave",		_b0d2,
	0xd2,	"aa",				0,
	0xd4,	"execute off-line immediate",	0,
//	0xd5,	"read log",			0,
//	0xd6,	"write log",			0,
	0xd8,	"enable operations",		0,
	0xd9,	"disable operations",		0,
	0xda,	"return status",			0,
};

Fetab _b0[] = {
	Ffeat,	smartfeat,	nelem(smartfeat),
	Flba8,	smlba8,	1,
	Flba16,	smlba16,	1,
	0,	0,	0,
};

Txtab _b0d0feat[] = {
	0xd0,	"",		0,
};

Fetab _b0d0[] = {
	Ffeat,	_b0d0feat,	nelem(_b0d0feat),
	Flba8,	smlba8,	1,
	Flba16,	smlba16,	1,
	0,	0,	0,
};


Txtab _b0d5feat[] = {
	0xd5,	"",		0,
};

Txtab _b0d5count[] = {
	0x01,	"",		0,
};

Txtab smpage[] = {
	0x00,	"page 0",		0,
	0x01,	"page 1",		0,
	0x02,	"page 2",		0,
	0x03,	"page 3",		0,
	0x04,	"page 4",		0,
	0x05,	"page 5",		0,
	0x06,	"page 6",		0,
	0x07,	"page 7",		0,
	0x08,	"page 8",		0,
	0x09,	"page 9",		0,
	0x11,	"page 17",	0,
	0xe0,	"sctstat",		0,
	0xe1,	"sctdata",	0,
};

Fetab _b0d5[] = {
	Ffeat,	_b0d5feat,	nelem(_b0d5feat),
//	Fsc,	_b0d5count,	nelem(_b0d5count),
	Flba0,	smpage,		nelem(smpage),
	Flba8,	smlba8,	1,
	Flba16,	smlba16,	1,
	0,	0,	0,
};

Fetab _2f[] = {
	Flba0,	smpage,		nelem(smpage),
	0,	0,		0,
};

Txtab nvfeat[] = {
	0x00,	"set power mode",			0,
	0x01,	"return from power mode",		0,
	0x10,	"add lbas",			0,
	0x11,	"remove lbas",			0,
	0x13,	"query pinned set",		0,
	0x13,	"query misses",			0,
	0x14,	"flush",				0,
	0x15,	"disable",			0,
	0x16,	"disable",			0,
};

Fetab _b6[] = {
	Ffeat,	nvfeat,		nelem(nvfeat),
	0,	0,		0,
};

Txtab umodes[] = {
	0x40,	"0",	0,
	0x41,	"1",	0,
	0x42,	"2",	0,
	0x43,	"3",	0,
	0x44,	"4",	0,
	0x45,	"5",	0,
	0x46,	"6",	0,
};

Fetab _ef0340[] = {
	Fsc,	umodes,	nelem(umodes),
	0,	0,	0,
};

Txtab txmode[] = {
	0x00,	"pio",		0,
	0x01,	"pio-iordy",	0,
	0x08,	"piofc",		0,
	0x20,	"mwdma",	0,
	0x40,	"udma",		_ef0340,
};

Fetab _ef03[] = {
	Fsc,	txmode,	nelem(txmode),
	0,	0,	0,
};

Txtab apmmode[] = {
	0xfe,	"maximum",	0,
	0x80,	"minimum without standby",	0,
	0x02,	"intermediate",	0,
	0x01,	"standby",	0,
};

Fetab _ef05[] = {
	Fsc,	apmmode,	nelem(apmmode),
	0,	0,	0,
};

Txtab scisone[] = {
	1,	"",		0,
};

Fetab _scis1[] = {
	Fsc,	scisone,	nelem(scisone),
	0,	0,	0,
};

Txtab feat[] = {
	0x01,	"enable 8-bit pio",	0,
	0x02,	"enable write cache",	0,
	0x03,	"set transfer mode",	_ef03,
	0x05,	"enable apm",	_ef05,
	0x06,	"enable power-up in standby",	0,
	0x07,	"power-up in standby device spin-up",	0,
	0x10,	"enable sata features",		0,
	0x0a,	"enable cfa power mode 1",	0,
	0x31,	"disable media status notification",	0,
	0x42,	"enable aam",	0,
	0x43,	"set maximum host interface sector times",	0,
	0x55,	"disable read look-ahead",	0,
	0x5d,	"enable release interrupt",	0,
	0x5e,	"enable service interrupt",	0,
	0x66,	"disable reverting to power-on defaults",	0,
	0x81,	"disable 8-bit pio",	0,
	0x82,	"disable write cache",	0,
	0x85,	"disable apm",	0,
	0x86,	"disable power-up in standby",	0,
	0x8a,	"disable cfa power mode 1",	0,
	0x10,	"disable sata features",		0,
	0x95,	"enable media status notification",	0,
	0xaa,	"enable read look-ahead",	0,
	0xc1,	"disable free-fall control", 0,
	0xc2,	"disable aam",	0,
	0xc3,	"sense data",	0,				/* incomplete; enable/disable */
	0xcc,	"enable reverting to power-on defaults",	0,
	0xdd,	"disable release interrupt",	0,
	0xde,	"disable service interrupt",	0,
};

Fetab _ef[] = {
	Ffeat,	feat,	nelem(feat),
	0,	0,	0,
};

/* 0xffff — sct command executing in background */
char *sctetab[] = {
	"Command complete without error",
	"Invalid Function Code",
	"Input LBA out of range"
	"Request 512-byte data block count overflow.", /* sic */
	"Invalid Function code in Error Recovery command",
	"Invalid Selection code in Error Recovery command",
	"Host read command timer is less than minimum value",
	"Host write command timer is less than minimum value",
	"Background SCT command was aborted because of an interrupting host command",
	"Background SCT command was terminated because of unrecoverable error",
	"Invalid Function code in SCT Read/Write Long command",
	"SCT data transfer command was issued without first issuing an SCT command",
	"Invalid Function code in SCT Feature Control command",
	"Invalid Feature code in SCT Feature Control command",
	"Invalid New State value in SCT Feature Control command",
	"Invalid Option Flags value in SCT Feature Control command",
	"Invalid SCT Action code",
	"Invalid Table ID (table not supported)",
	"Command wa saborted due to device security being locked",
	"Invalid revision code in SCT data",
	"Foreground SCT operation was terminated because of unrecoverable error",
	"Error Recovery Timer expired",	/* sic */
};

Txtab fcfewcrt[] = {
	1,	"enable",		0,
	2,	"disable",	0,
};

Fetab fcfewcr[] = {
	Sstate,	fcfewcrt,	nelem(fcfewcrt),
	0,	0,	0,
};

Txtab fcfewct[] = {
	1,	"set features",	0,
	2,	"enable",		0,
	3,	"disable",	0,
};

Fetab fcfewc[] = {
	Sstate,	fcfewct,	nelem(fcfewct),
	0,	0,	0,
};

Txtab fcfn[] = {
	1,	"set state",	0,
	2,	"return state",	0,
	3,	"return feature option flags",	0,
};

Txtab fcfe[] = {
	2,	"write cache reordering",	fcfewcr,
	1,	"write cache",	fcfewc,
	3,	"temperature logging interval", 0,
};

Txtab fcoptf[] = {
	1,	"preserve",	0,
};

Txtab fcproto[] = {
	Pnd,	"",	0,
};

Fetab sctfc[] = {
	Sfn,	fcfn,	nelem(fcfn),
	Sfe,	fcfe,	nelem(fcfe),
	Soptf,	fcoptf,	nelem(fcoptf),
	Pbase,	fcproto,	nelem(fcproto),
	0,	0,	0,
};

Txtab sctdt[] = {
	Stabid,	"tableid",	0,
};

Txtab tabnam[] = {
	2,	"hda temperature history",	0,
};

Txtab tablefc[] = {
	1,	"",	0,
};

Fetab tables[] = {
	Sfn,	tablefc,	nelem(tablefc),
	Stabid,	tabnam,	nelem(tabnam),
	0,	0,	0,
};

Txtab ersc[] = {
	1,	"read timer",	0,
	2,	"write timer",	0,
};

Txtab erfc[] = {
	1,	"set",	0,
	2,	"return",	0,
};

Txtab erti[] = {
	-1,	"=",	0,
};

Fetab scter[] = {
	Sfn,	erfc,	nelem(erfc),
	Ssc,	ersc,	nelem(ersc),
	Stimer,	erti,	nelem(erti),
	Pbase,	fcproto,	nelem(fcproto),
	0,	0,	0,
};

Fetab patfe[] = {
	Pbase,	fcproto,	nelem(fcproto),
	0,	0,	0,
};

Txtab wsfc[] = {
	1,	"repeat write pattern",		patfe,
	2,	"repeat write data block",		0,
	0x101,	"repeat write pattern foreground",	patfe,
	0x102,	"repeat write data block foreground",	0,
};

Txtab wslba[] = {
	-1,	"lba",	0,
};

Txtab wscnt[] = {
	-1,	"count",	0,
};

Txtab wspat[] = {
	-1,	"pattern",	0,
};

Fetab wsame[] = {
	Sfn,	wsfc,	nelem(wsfc),
	Slba,	wslba,	nelem(wslba),
	Scnt,	wscnt,	nelem(wscnt),
	Spat,	wspat,	nelem(wspat),
	0,	0,	0,
};

Txtab action[] = {
	5,	"read data table",	tables,
	4,	"feature control",	sctfc,
	3,	"error recovery time", scter,
	2,	"write same",	wsame,
};

Fetab scta[] = {
	Saction,	action,	nelem(action),
	0,	0,	0,
};

Atatab atatab[] = {
0x00,	0,	0,	Pnd|P28,		0,	0,	"nop",
0x03,	0,	Cmdn,	Pnd|P28,		0,	0,	"cfa request extended error",
0x08,	Cmdn,	0,	Preset|P28,		0,	0,	"device reset",
0x0b,	0,	Cmdp,	Pnd|P48,		0,	0,	"request sense data ext",
0x20,	0,	0,	Pin|Ppio|P28,		0,	iofmt,	"read sector",
0x24,	0,	Cmdn,	Pin|Ppio|P48,		0,	iofmt,	"read sector ext",
0x25,	0,	Cmdn,	Pin|Pdma|P48,		0,	iofmt,	"read dma ext",
0x26,	0,	Cmdn,	Pin|Pdmq|P48,		0,	iofmt,	"read dma queued ext",
0x27,	0,	Cmdn,	Pnd|P48,		0,	0,	"read native max address ext",
0x29,	0,	Cmdn,	Pin|Ppio|P48,		0,	iofmt,	"read multiple ext",
0x2a,	0,	Cmdn,	Pin|Pdma|P48,		0,	iofmt,	"read stream dma ext",
0x2b,	0,	Cmdn,	Pin|Ppio|P48,		0,	iofmt,	"read stream ext",
0x2f,	Cmd5sc,	0,	Pin|Ppio|P48|P512,	_2f,	glfmt,	"read log ext",
0x2f,	Cmd5sc,	0,	Psct|Pin|Ppio|P48|P512,	scta,	0,	"sct",
0x30,	0,	Cmdn,	Pout|Ppio|P28,		0,	0,	"write sector",
0x34,	0,	Cmdn,	Pout|Ppio|P48,		0,	0,	"write sector ext",
0x35,	0,	Cmdn,	Pout|Pdma|P48,		0,	0,	"write dma ext",
0x36,	0,	Cmdn,	Pout|Pdmq|P48,		0,	0,	"write dma queued ext",
0x37,	0,	Cmdn,	Pnd|P48,		0,	0,	"set max address ext",
0x38,	0,	Cmdn,	Pout|Ppio|P28,		0,	0,	"cfa write sectors without erase",
0x39,	0,	Cmdn,	Pout|Ppio|P48,		0,	0,	"write multiple ext",
0x3a,	0,	Cmdn,	Pout|Pdma|P48,		0,	0,	"write stream dma ext",
0x3b,	0,	Cmdn,	Pout|Ppio|P48,		0,	0,	"write stream ext",
0x3d,	0,	Cmdn,	Pout|Pdma|P48,		0,	0,	"write dma fua ext",
0x3e,	0,	Cmdn,	Pout|Pdmq|P48,		0,	0,	"write dma queued fua ext",
0x3f,	0,	0,	Pout|Ppio|P48,		0,	0,	"write log ext",
0x40,	0,	Cmdn,	Pnd|P28,		0,	0,	"read verify sector",
0x42,	0,	Cmdn,	Pnd|P48,		0,	0,	"read verify sector ext",
0x45,	0,	Cmdn,	Pnd|P48,		0,	0,	"write uncorrectable ext",
0x47,	Cmd5sc,	0,	Pin|Pdma|P48|P512,	_2f,	glfmt,	"read log dma ext",
0x51,	0,	0,	Pnd|P48,		0,	0,	"configure stream",
0x57,	0,	0,	Pout|Pdma|P48,		0,	0,	"write log dma ext",
0x5b,	0,	Cmdp,	Pnd|P28,		0,	0,	"trusted non-data",
0x5c,	0,	Cmdp,	Pin|Ppio|P28,		0,	iofmt,	"trusted receive",
0x5d,	0,	Cmdp,	Pin|Pdma|P28,		0,	iofmt,	"trusted receive dma",
0x5e,	0,	Cmdp,	Pout|Ppio|P28,		0,	0,	"trusted send",
0x5f,	0,	Cmdp,	Pout|Pdma|P28,		0,	0,	"trusted send dma",
0x60,	0,	Cmdn,	Pin|Pdmq|P48,		0,	iofmt,	"read fpdma queued",
0x61,	0,	Cmdn,	Pout|Pdmq|P48,		0,	0,	"write fpdma queued",
0x87,	0,	Cmdn,	Pin|Ppio|P28,		0,	iofmt,	"cfa translate sector",
0x90,	0,	0,	Pdiag|P28,		0,	0,	"execute device diagnostic",
0x92,	0,	Cmdn,	Pout|Ppio|P28,		0,	0,	"download microcode",
0x93,	0,	Cmdn,	Pout|Pdma|P28,		0,	0,	"download microcode dma",
0xa0,	Cmdn,	0,	Ppkt,			0,	0,	"packet",
0xa1,	Cmdn,	0,	Pin|Ppio|P28|P512,	_scis1,	idfmt,	"identify packet device",
0xb0,	Cmd5sc,	Cmdn,	Pin|Ppio|P28|P512,	 _b0d0,	sdfmt,	"smart read data",
0xb0,	Cmd5sc,	Cmdn,	Pin|Ppio|P28|P512,	 _b0d5,	slfmt,	"smart read log",
0xb0,	0,	Cmdn,	Pnd|P28,		 _b0,	smfmt,	"smart",
0xb1,	0,	0,	Pnd|P28,		0,	0,	"device configuration overlay",
0xb6,	0,	Cmdn,	Pnd|P48,		0,	0,	"nv cache",
0xc0,	Cmdf,	Cmdn,	Pnd|P28,		0,	0,	"cfa erase sectors",
0xc4,	0,	Cmdn,	Pin|Ppio|P28,		0,	iofmt,	"read multiple",
0xc5,	0,	Cmdn,	Pout|Ppio|P28,		0,	0,	"write multiple",
0xc6,	0,	Cmdn,	Pnd|P28,		0,	0,	"set multiple mode",
0xc7,	0,	Cmdn,	Pin|Pdmq|P28,		0,	iofmt,	"read dma queued",
0xc8,	0,	Cmdn,	Pin|Pdma|P28,		0,	iofmt,	"read dma",
0xca,	0,	Cmdn,	Pout|Pdma|P28,		0,	0,	"write dma",
0xcc,	0,	Cmdn,	Pout|Pdmq|P28,		0,	0,	"write dma queued",
0xcd,	0,	Cmdn,	Pout|Ppio|P28,		0,	0,	"cfa write multiple without erase",
0xce,	0,	Cmdn,	Pout|Ppio|P48,		0,	0,	"write multiple fua ext",
0xd1,	0,	Cmdn,	Pnd|P28,		0,	0,	"check media card type",
0xda,	0,	Cmdn,	Pnd|P28,		0,	0,	"get media status",
0xe0,	0,	0,	Pnd|P28,		0,	0,	"standby immediate",
0xe1,	0,	0,	Pnd|P28,		0,	0,	"idle immediate",
0xe2,	0,	0,	Pnd|P28,		0,	0,	"standby",
0xe3,	0,	0,	Pnd|P28,		0,	0,	"idle",
0xe4,	0,	Cmdn,	Pin|Ppio|P28,		0,	iofmt,	"read buffer",
0xe5,	0,	0,	Pnd|P28,		0,	0,	"check power mode",
0xe6,	0,	0,	Pnd|P28,		0,	0,	"sleep",
0xe7,	0,	0,	Pnd|P28,		0,	0,	"flush cache",
0xe8,	0,	Cmdn,	Pout|Ppio|P28,		0,	0,	"write buffer",
0xe9,	0,	Cmdn,	Pin|Pdma|P28,		0,	iofmt,	"read buffer dma",
0xea,	0,	Cmdn,	Pnd|P28,		0,	0,	"flush cache ext",
0xeb,	0,	Cmdn,	Pdma|P28,		0,	0,	"write buffer dma",
0xec,	0,	0,	Pin|Ppio|P28|P512,	_scis1,	idfmt,	"identify device",
0xef,	0,	0,	Pnd|P28,	 	_ef,	0,	"set features",
0xf1,	0,	0,	Pout|Ppio|P28,		0,	0,	"security set password",
0xf2,	0,	0,	Pout|Ppio|P28,		0,	0,	"security unlock",
0xf3,	0,	0,	Pnd|P28,		0,	0,	"security erase prepare",
0xf4,	0,	0,	Pout|Ppio|P28,		0,	0,	"security erase unit",
0xf5,	0,	0,	Pnd|P28,		0,	0,	"security freeze lock",
0xf6,	0,	0,	Pout|Ppio|P28,		0,	0,	"security disable password",
0xf8,	0,	0,	Pnd|P28,		0,	0,	"read native max address",
0xf9,	0,	0,	Pnd|P28,		0,	0,	"set max address",
0xf000,	0,	0,	Pnd|P28,		0,	sigfmt,	"signature",
0xf100,	0,	0,	Pnd|P28,		0,	0,	"oobreset",
};
