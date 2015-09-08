#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "mothra.h"
#include "html.h"
Tag tag[]={
[Tag_a]		"a",		END,
[Tag_abbr]	"abbr",		END,
[Tag_acronym]	"acronym",	END,
[Tag_address]	"address",	END,
[Tag_applet]	"applet",	NOEND,
[Tag_audio]	"audio",	OPTEND,
[Tag_b]		"b",		END,
[Tag_base]	"base",		NOEND,
[Tag_blockquot]	"blockquote",	END,
[Tag_body]	"body",		END,	/* OPTEND */
[Tag_br]	"br",		NOEND,
[Tag_button]	"button",	END,
[Tag_center]	"center",	END,
[Tag_cite]	"cite",		END,
[Tag_code]	"code",		END,
[Tag_comment]	"!--",		NOEND,
[Tag_dd]	"dd",		NOEND,	/* OPTEND */
[Tag_del]	"del",		END,
[Tag_dfn]	"dfn",		END,
[Tag_dir]	"dir",		END,
[Tag_div]	"div",		END,	/* OPTEND */
[Tag_dl]	"dl",		END,
[Tag_dt]	"dt",		NOEND,	/* OPTEND */
[Tag_em]	"em",		END,
[Tag_embed]	"embed",	NOEND,
[Tag_end]	0,		ERR,
[Tag_font]	"font",		END,
[Tag_form]	"form",		END,
[Tag_frame]	"frame",	NOEND,
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
[Tag_iframe]	"iframe",	NOEND,
[Tag_img]	"img",		NOEND,
[Tag_image]	"image",	NOEND,
[Tag_input]	"input",	NOEND,
[Tag_ins]	"ins",		END,
[Tag_isindex]	"isindex",	NOEND,
[Tag_kbd]	"kbd",		END,
[Tag_key]	"key",		END,
[Tag_li]	"li",		NOEND,	/* OPTEND */
[Tag_link]	"link",		NOEND,
[Tag_listing]	"listing",	END,
[Tag_menu]	"menu",		END,
[Tag_meta]	"meta",		NOEND,
[Tag_nextid]	"nextid",	NOEND,
[Tag_object]	"object",	END,
[Tag_ol]	"ol",		END,
[Tag_option]	"option",	NOEND,	/* OPTEND */
[Tag_p]		"p",		NOEND,	/* OPTEND */
[Tag_plaintext]	"plaintext",	NOEND,
[Tag_pre]	"pre",		END,
[Tag_s]		"s",		END,
[Tag_samp]	"samp",		END,
[Tag_script]	"script",	END,
[Tag_select]	"select",	END,
[Tag_span]	"span",		END,
[Tag_strike]	"strike",	END,
[Tag_strong]	"strong",	END,
[Tag_style]	"style",	END,
[Tag_sub]	"sub",		END,
[Tag_sup]	"sup",		END,
[Tag_source]	"source",	NOEND,
[Tag_table]	"table",	END,
[Tag_td]	"td",		END,
[Tag_th]	"th",		END,
[Tag_textarea]	"textarea",	END,
[Tag_title]	"title",	END,
[Tag_tr]	"tr",		END,
[Tag_tt]	"tt",		END,
[Tag_u]		"u",		END,
[Tag_ul]	"ul",		END,
[Tag_var]	"var",		END,
[Tag_video]	"video",	OPTEND,
[Tag_wbr]	"wbr",		NOEND,
[Tag_xmp]	"xmp",		END,
};
