enum
{
	Maxfdata	= IOUNIT,
	Maxiosize	= IOHDRSZ+Maxfdata,
};

extern Fcall	*req;
extern Fcall	*rep;
extern char	repdata[Maxfdata];
extern uchar	statbuf[STATMAX];
