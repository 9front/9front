/* color.c */
extern	void		choosecolor(Vnc*);
extern	void		(*cvtpixels)(uchar*, uchar*, int);
extern  void            settranslation(Vnc*);

/* draw.c */
extern	void		sendencodings(Vnc*);
extern	void		requestupdate(Vnc*, int);
extern	void		readfromserver(Vnc*);

extern	uchar	zero[];

/* vncv.c */
extern	char		*charset;
extern	char		*encodings;
extern	int		autoscale;
extern	int		bpp12;
extern	Vnc*		vnc;
extern	int		mousefd;

/* wsys.c */
extern	void		adjustwin(Vnc*, int);
extern	void		readkbd(Vnc*);
extern	void		initmouse(void);
extern	void		mousewarp(Point);
extern	void		readmouse(Vnc*);
extern  void            senddim(Vnc*);
extern  void            writesnarf(Vnc*, long);
extern  void            checksnarf(Vnc*);
