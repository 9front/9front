#include <u.h>
#include <libc.h>
#include <bio.h>
#include "pci.h"
#include "vga.h"

// VESA Monitor Timing Standard mode definitions as per
// VESA and Industry Standards and Guidelines for Computer
// Display Monitor Timing, Version 1.0, Revision 0.8, 17 September 1998.
//
// See /lib/vesa/dmtv1r08.pdf.
//
// This might go back into vgadb at some point. It's here mainly
// so that people don't change it, and so that we can run without vgadb.

static Mode vesa640x480x60 = {
	.name = "640x480@60Hz",
	.x = 640,
	.y = 480,

	.ht = 800,
	.shb = 656,
	.ehb = 656+96,

	.vt = 525,
	.vrs = 490,
	.vre = 490+2,

	.frequency = 25175000,

	.hsync = '-',
	.vsync = '-',
	.interlace = '\0',
};

static Mode vesa640x480x72 = {
	.name = "640x480@72Hz",
	.x = 640,
	.y = 480,

	.ht = 832,
	.shb = 664,
	.ehb = 664+40,

	.vt = 520,
	.vrs = 489,
	.vre = 489+3,

	.frequency = 31500000,

	.hsync = '-',
	.vsync = '-',
	.interlace = '\0',
};

static Mode vesa640x480x75 = {
	.name = "640x480@75Hz",
	.x = 640,
	.y = 480,

	.ht = 840,
	.shb = 656,
	.ehb = 656+64,

	.vt = 500,
	.vrs = 481,
	.vre = 481+3,

	.frequency = 31500000,

	.hsync = '-',
	.vsync = '-',
	.interlace = '\0',
};

static Mode vesa640x480x85 = {
	.name = "640x480@85Hz",
	.x = 640,
	.y = 480,

	.ht = 832,
	.shb = 696,
	.ehb = 696+56,

	.vt = 509,
	.vrs = 481,
	.vre = 481+3,

	.frequency = 36000000,

	.hsync = '-',
	.vsync = '-',
	.interlace = '\0',
};

static Mode vesa800x600x56 = {
	.name = "800x600@56Hz",
	.x = 800,
	.y = 600,

	.ht = 1024,
	.shb = 824,
	.ehb = 824+72,

	.vt = 625,
	.vrs = 601,
	.vre = 601+2,

	.frequency = 36000000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa800x600x60 = {
	.name = "800x600@60Hz",
	.x = 800,
	.y = 600,

	.ht = 1056,
	.shb = 840,
	.ehb = 840+128,

	.vt = 628,
	.vrs = 601,
	.vre = 601+4,

	.frequency = 40000000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa800x600x72 = {
	.name = "800x600@72Hz",
	.x = 800,
	.y = 600,

	.ht = 1040,
	.shb = 856,
	.ehb = 856+120,

	.vt = 666,
	.vrs = 637,
	.vre = 637+6,

	.frequency = 50000000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa800x600x75 = {
	.name = "800x600@75Hz",
	.x = 800,
	.y = 600,

	.ht = 1056,
	.shb = 816,
	.ehb = 816+80,

	.vt = 625,
	.vrs = 601,
	.vre = 601+3,

	.frequency = 49500000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa800x600x85 = {
	.name = "800x600@85Hz",
	.x = 800,
	.y = 600,

	.ht = 1048,
	.shb = 832,
	.ehb = 832+64,

	.vt = 631,
	.vrs = 601,
	.vre = 601+3,

	.frequency = 56250000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1024x768x60 = {
	.name = "1024x768@60Hz",
	.x = 1024,
	.y = 768,

	.ht = 1344,
	.shb = 1048,
	.ehb = 1048+136,

	.vt = 806,
	.vrs = 771,
	.vre = 771+6,

	.frequency = 65000000,

	.hsync = '-',
	.vsync = '-',
	.interlace = '\0',
};

static Mode vesa1024x768x70 = {
	.name = "1024x768@70Hz",
	.x = 1024,
	.y = 768,

	.ht = 1328,
	.shb = 1048,
	.ehb = 1048+136,

	.vt = 806,
	.vrs = 771,
	.vre = 771+6,

	.frequency = 75000000,

	.hsync = '-',
	.vsync = '-',
	.interlace = '\0',
};

static Mode vesa1024x768x75 = {
	.name = "1024x768@75Hz",
	.x = 1024,
	.y = 768,

	.ht = 1312,
	.shb = 1040,
	.ehb = 1040+96,

	.vt = 800,
	.vrs = 769,
	.vre = 769+3,

	.frequency = 78750000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1024x768x85 = {
	.name = "1024x768@85Hz",
	.x = 1024,
	.y = 768,

	.ht = 1376,
	.shb = 1072,
	.ehb = 1072+96,

	.vt = 808,
	.vrs = 769,
	.vre = 769+3,

	.frequency = 94500000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1152x864x75 = {
	.name = "1152x864@75Hz",
	.x = 1152,
	.y = 864,

	.ht = 1600,
	.shb = 1216,
	.ehb = 1216+128,

	.vt = 900,
	.vrs = 865,
	.vre = 865+3,

	.frequency = 108000000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1280x960x60 = {
	.name = "1280x960@60Hz",
	.x = 1280,
	.y = 960,

	.ht = 1800,
	.shb = 1376,
	.ehb = 1376+112,

	.vt = 1000,
	.vrs = 961,
	.vre = 961+3,

	.frequency = 108000000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1280x960x85 = {
	.name = "1280x960@85Hz",
	.x = 1280,
	.y = 960,

	.ht = 1728,
	.shb = 1344,
	.ehb = 1344+160,

	.vt = 1011,
	.vrs = 961,
	.vre = 961+3,

	.frequency = 148500000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1280x1024x60 = {
	.name = "1280x1024@60Hz",
	.x = 1280,
	.y = 1024,

	.ht = 1688,
	.shb = 1328,
	.ehb = 1328+112,

	.vt = 1066,
	.vrs = 1025,
	.vre = 1025+3,

	.frequency = 108000000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1280x1024x75 = {
	.name = "1280x1024@75Hz",
	.x = 1280,
	.y = 1024,

	.ht = 1688,
	.shb = 1296,
	.ehb = 1296+144,

	.vt = 1066,
	.vrs = 1025,
	.vre = 1025+3,

	.frequency = 135000000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1280x1024x85 = {
	.name = "1280x1024@85Hz",
	.x = 1280,
	.y = 1024,

	.ht = 1728,
	.shb = 1344,
	.ehb = 1344+160,

	.vt = 1072,
	.vrs = 1025,
	.vre = 1025+3,

	.frequency = 157500000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1600x1200x60 = {
	.name = "1600x1200@60Hz",
	.x = 1600,
	.y = 1200,

	.ht = 2160,
	.shb = 1664,
	.ehb = 1664+192,

	.vt = 1250,
	.vrs = 1201,
	.vre = 1201+3,

	.frequency = 162000000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1600x1200x65 = {
	.name = "1600x1200@65Hz",
	.x = 1600,
	.y = 1200,

	.ht = 2160,
	.shb = 1664,
	.ehb = 1664+192,

	.vt = 1250,
	.vrs = 1201,
	.vre = 1201+3,

	.frequency = 175500000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1600x1200x70 = {
	.name = "1600x1200@70Hz",
	.x = 1600,
	.y = 1200,

	.ht = 2160,
	.shb = 1664,
	.ehb = 1664+192,

	.vt = 1250,
	.vrs = 1201,
	.vre = 1201+3,

	.frequency = 189000000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1600x1200x75 = {
	.name = "1600x1200@75Hz",
	.x = 1600,
	.y = 1200,

	.ht = 2160,
	.shb = 1664,
	.ehb = 1664+192,

	.vt = 1250,
	.vrs = 1201,
	.vre = 1201+3,

	.frequency = 202500000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1600x1200x85 = {
	.name = "1600x1200@85Hz",
	.x = 1600,
	.y = 1200,

	.ht = 2160,
	.shb = 1664,
	.ehb = 1664+192,

	.vt = 1250,
	.vrs = 1201,
	.vre = 1201+3,

	.frequency = 229500000,

	.hsync = '+',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1792x1344x60 = {
	.name = "1792x1344@60Hz",
	.x = 1792,
	.y = 1344,

	.ht = 2448,
	.shb = 1920,
	.ehb = 1920+200,

	.vt = 1394,
	.vrs = 1345,
	.vre = 1345+3,

	.frequency = 204750000,

	.hsync = '-',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1792x1344x75 = {
	.name = "1792x1344@75Hz",
	.x = 1792,
	.y = 1344,

	.ht = 2456,
	.shb = 1888,
	.ehb = 1888+216,

	.vt = 1417,
	.vrs = 1345,
	.vre = 1345+3,

	.frequency = 261000000,

	.hsync = '-',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1856x1392x60 = {
	.name = "1856x1392@60Hz",
	.x = 1856,
	.y = 1392,

	.ht = 2528,
	.shb = 1952,
	.ehb = 1952+224,

	.vt = 1439,
	.vrs = 1393,
	.vre = 1393+3,

	.frequency = 218250000,

	.hsync = '-',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1856x1392x75 = {
	.name = "1856x1392@75Hz",
	.x = 1856,
	.y = 1392,

	.ht = 2560,
	.shb = 1984,
	.ehb = 1984+224,

	.vt = 1500,
	.vrs = 1393,
	.vre = 1393+3,

	.frequency = 288000000,

	.hsync = '-',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1920x1440x60 = {
	.name = "1920x1440@60Hz",
	.x = 1920,
	.y = 1440,

	.ht = 2600,
	.shb = 2048,
	.ehb = 2048+208,

	.vt = 1500,
	.vrs = 1441,
	.vre = 1441+3,

	.frequency = 234000000,

	.hsync = '-',
	.vsync = '+',
	.interlace = '\0',
};

static Mode vesa1920x1440x75 = {
	.name = "1920x1440@75Hz",
	.x = 1920,
	.y = 1440,

	.ht = 2640,
	.shb = 2064,
	.ehb = 2064+224,

	.vt = 1500,
	.vrs = 1441,
	.vre = 1441+3,

	.frequency = 297000000,

	.hsync = '-',
	.vsync = '+',
	.interlace = '\0',
};

Mode *vesamodes[] = {
	&vesa640x480x60,
	&vesa640x480x72,
	&vesa640x480x75,
	&vesa640x480x85,
	&vesa800x600x56,
	&vesa800x600x60,
	&vesa800x600x72,
	&vesa800x600x75,
	&vesa800x600x85,
	&vesa1024x768x60,
	&vesa1024x768x70,
	&vesa1024x768x75,
	&vesa1024x768x85,
	&vesa1152x864x75,
	&vesa1280x960x60,
	&vesa1280x960x85,
	&vesa1280x1024x60,
	&vesa1280x1024x75,
	&vesa1280x1024x85,
	&vesa1600x1200x60,
	&vesa1600x1200x65,
	&vesa1600x1200x70,
	&vesa1600x1200x75,
	&vesa1600x1200x85,
	&vesa1792x1344x60,
	&vesa1792x1344x75,
	&vesa1856x1392x60,
	&vesa1856x1392x75,
	&vesa1920x1440x60,
	&vesa1920x1440x75,
	0
};
