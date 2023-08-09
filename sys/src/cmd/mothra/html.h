/*
 * Parameters
 */
#define	NSTACK	100	/* html grammar is not recursive, so 30 or so should do */
#define	NHBUF	IOUNIT	/* Input buffer size */
#define	NPEEKC	3	/* Maximum lookahead */
#define	NTOKEN	65536	/* Maximum token length */
#define	NATTR	512	/* Maximum number of attributes of a tag */
typedef struct Pair Pair;
typedef struct Tag Tag;
typedef struct Stack Stack;
typedef struct Hglob Hglob;
typedef struct Form Form;
typedef struct Entity Entity;
struct Pair{
	char *name;
	char *value;
};
struct Entity{
	char *name;
	Rune value;
};
struct Tag{
	char *name;
	int action;
};
struct Stack{
	int tag;		/* html tag being processed */
	int pre;		/* in preformatted text? */
	int font;		/* typeface */
	int size;		/* point size of text */
	int sub;		/* < 0 superscript, > 0 subscript */
	int margin;		/* left margin position */
	int indent;		/* extra indent at paragraph start */
	int number;		/* paragraph number */
	int ismap;		/* flag of <img> */
	int isscript;		/* inside <script> */
	int strike;		/* flag of <strike> */
	int width;		/* size of image */
	int height;
	char *image;		/* arg of <img> */
	char *link;		/* arg of <a href=...> */
	char *name;		/* arg of <a name=...> */
};

/*
 * Globals -- these are packed up into a struct that gets passed around
 * so that multiple parsers can run concurrently
 */
struct Hglob{
	char *tp;		/* pointer in text buffer */
	char *name;		/* input file name */
	int hfd;		/* input file descriptor */
	char hbuf[NHBUF];	/* input buffer */
	char *hbufp;		/* next character in buffer */
	char *ehbuf;		/* end of good characters in buffer */
	int heof;		/* end of file flag */
	int peekc[NPEEKC];	/* characters to re-read */
	int npeekc;		/* # of characters to re-read */
	char token[NTOKEN];	/* if token type is TEXT */
	Pair attr[NATTR];	/* tag attribute/value pairs */
	int nsp;		/* # of white-space characters before TEXT token */
	int spacc;		/* place to accumulate more spaces */
				/* if negative, won't accumulate! */
	int tag;		/* if token type is TAG or END */
	Stack stack[NSTACK];	/* parse stack */
	Stack *state;		/* parse stack pointer */
	int lineno;		/* input line number */
	int linebrk;		/* flag set if we require a line-break in output */
	int para;		/* flag set if we need an indent at the break */
	char *text;		/* text buffer */
	char *etext;		/* end of text buffer */
	Form *form;		/* data for form under construction */
	Www *dst;		/* where the text goes */
};

/*
 * Token types
 */
enum{
	TAG=1,
	ENDTAG,
	TEXT,
};

/*
 * Magic characters corresponding to
 *	literal < followed by / ! or alpha,
 *	literal > and
 *	end of file
 */
#define STAG	65536
#define ETAG	65537
#define EOF	-1

/*
 * fonts
 */
enum{
	ROMAN,
	ITALIC,
	BOLD,
	CWIDTH,
};

/*
 * font sizes
 */
enum{
	SMALL,
	NORMAL,
	LARGE,
	ENORMOUS,
};

/*
 * length direction
 */
enum{
	HORIZ,
	VERT,
};
int strtolength(Hglob *g, int dir, char *str);

/*
 * Token names for the html parser.
 * Tag_end corresponds to </end> tags.
 * Tag_text tags text not in a tag.
 * Those two must follow the others.
 */
enum{
	Tag_comment,

	Tag_a,
	Tag_abbr,
	Tag_acronym,
	Tag_address,
	Tag_applet,
	Tag_article,
	Tag_audio,
	Tag_b,
	Tag_base,
	Tag_blockquot,
	Tag_body,
	Tag_br,
	Tag_button,
	Tag_center,
	Tag_cite,
	Tag_code,
	Tag_dd,
	Tag_del,
	Tag_div,
	Tag_dfn,
	Tag_dir,
	Tag_dl,
	Tag_dt,
	Tag_em,
	Tag_embed,
	Tag_figure,
	Tag_figcaption,
	Tag_font,
	Tag_form,
	Tag_frame,	/* rm 5.8.97 */
	Tag_h1,
	Tag_h2,
	Tag_h3,
	Tag_h4,
	Tag_h5,
	Tag_h6,
	Tag_head,
	Tag_hr,
	Tag_html,
	Tag_i,
	Tag_iframe,
	Tag_img,
	Tag_image,
	Tag_input,
	Tag_ins,
	Tag_isindex,
	Tag_kbd,
	Tag_key,
	Tag_li,
	Tag_link,
	Tag_listing,
	Tag_menu,
	Tag_meta,
	Tag_nextid,
	Tag_object,
	Tag_ol,
	Tag_option,
	Tag_p,
	Tag_plaintext,
	Tag_pre,
	Tag_s,
	Tag_samp,
	Tag_script,
	Tag_select,
	Tag_span,
	Tag_strike,
	Tag_strong,
	Tag_style,
	Tag_sub,
	Tag_sup,
	Tag_source,
	Tag_table,	/* rm 3.8.00 */
	Tag_td,
	Tag_th,
	Tag_textarea,
	Tag_title,
	Tag_tr,
	Tag_tt,
	Tag_u,
	Tag_ul,
	Tag_var,
	Tag_video,
	Tag_wbr,
	Tag_xmp,

	Tag_end,	/* also used to indicate unrecognized start tag */
	Tag_text,
};
enum{
	NTAG=Tag_end,
	END=1,	/* tag must have a matching end tag */
	NOEND,	/* tag must not have a matching end tag */
	OPTEND,	/* tag may have a matching end tag */
	ERR,		/* tag must not occur */
};
Tag tag[];
void rdform(Hglob *);
void endform(Hglob *);
char *pl_getattr(Pair *, char *);
int pl_hasattr(Pair *, char *);
void pl_htmloutput(Hglob *, int, char *, Field *);

#pragma incomplete Form
#pragma incomplete Field

