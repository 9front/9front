#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "mothra.h"
#include "html.h"
Tag tag[]={
[Tag_comment]	"!--",		NOEND,
[Tag_a]		"a",		END,
[Tag_address]	"address",	END,
[Tag_b]		"b",		END,
[Tag_base]	"base",		NOEND,
[Tag_blockquot]	"blockquote",	END,
[Tag_body]	"body",		END,	/* OPTEND */
[Tag_br]	"br",		NOEND,
[Tag_center]	"center",	END,
[Tag_cite]	"cite",		END,
[Tag_code]	"code",		END,
[Tag_dd]	"dd",		NOEND,	/* OPTEND */
[Tag_dfn]	"dfn",		END,
[Tag_dir]	"dir",		END,
[Tag_dl]	"dl",		END,
[Tag_dt]	"dt",		NOEND,	/* OPTEND */
[Tag_em]	"em",		END,
[Tag_font]	"font",		END,
[Tag_form]	"form",		END,
[Tag_h1]	"h1",		END,
[Tag_h2]	"h2",		END,
[Tag_h3]	"h3",		END,
[Tag_h4]	"h4",		END,
[Tag_h5]	"h5",		END,
[Tag_h6]	"h6",		END,
[Tag_head]	"head",		END,	/* OPTEND */
[Tag_hr]	"hr",		NOEND,
[Tag_html]	"html",		END,	/* OPTEND */
[Tag_i]		"i",		END,
[Tag_input]	"input",	NOEND,
[Tag_img]	"img",		NOEND,
[Tag_isindex]	"isindex",	NOEND,
[Tag_kbd]	"kbd",		END,
[Tag_key]	"key",		END,
[Tag_li]	"li",		NOEND,	/* OPTEND */
[Tag_link]	"link",		NOEND,
[Tag_listing]	"listing",	END,
[Tag_menu]	"menu",		END,
[Tag_meta]	"meta",		NOEND,
[Tag_nextid]	"nextid",	NOEND,
[Tag_ol]	"ol",		END,
[Tag_option]	"option",	NOEND,	/* OPTEND */
[Tag_p]		"p",		NOEND,	/* OPTEND */
[Tag_plaintext]	"plaintext",	NOEND,
[Tag_pre]	"pre",		END,
[Tag_samp]	"samp",		END,
[Tag_script]	"script",	END,
[Tag_style]	"style",	END,
[Tag_select]	"select",	END,
[Tag_strong]	"strong",	END,
[Tag_table]		"table",	END,
[Tag_td]		"td",		END,
[Tag_textarea]	"textarea",	END,
[Tag_title]	"title",	END,
[Tag_tr]	"tr",		END,
[Tag_tt]	"tt",		END,
[Tag_u]		"u",		END,
[Tag_ul]	"ul",		END,
[Tag_var]	"var",		END,
[Tag_xmp]	"xmp",		END,
[Tag_frame]	"frame",	NOEND,
[Tag_end]	0,		ERR,
};
