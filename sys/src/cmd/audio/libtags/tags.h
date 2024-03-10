#pragma lib "/sys/src/cmd/audio/libtags/libtags.a$O"

typedef struct Tagctx Tagctx;
typedef int (*Tagread)(void *buf, int *cnt);

/* Tag type. */
enum
{
	Tunknown = -1,
	Tartist,
	Talbum,
	Ttitle,
	Tdate, /* "2014", "2015/02/01", but the year goes first */
	Ttrack, /* "1", "01", "1/4", but the track number goes first */
	Talbumgain, /* see GAIN note */
	Talbumpeak,
	Ttrackgain, /* see GAIN note */
	Ttrackpeak,
	Tgenre,
	Timage,
	Tcomposer,
	Tcomment,
	Talbumartist,
};

/* GAIN note:
 *
 * Even though the gain value may be expected to look as "52 dB", it
 * very well may be a plain integer (such as "12032") for R128_* tags.
 * To do things correctly you will have to check 'const char *k' argument
 * passed into your callback and parse it accordingly.
 *
 * FIXME the output gain (opus) is not parsed, btw.
 */

/* Format of the audio file. */
enum
{
	Funknown = -1,
	Fmp3,
	Fvorbis,
	Fflac,
	Fm4a,
	Fopus,
	Fwav,
	Fit,
	Fxm,
	Fs3m,
	Fmod,

	Fmax,
};

/* Tag parser context. You need to set it properly before parsing an audio file using libtags. */
struct Tagctx
{
	/* Read function. This is what libtags uses to read the file. */
	int (*read)(Tagctx *ctx, void *buf, int cnt);

	/* Seek function. This is what libtags uses to seek through the file. */
	int (*seek)(Tagctx *ctx, int offset, int whence);

	/* Callback that is used by libtags to inform about the tags of a file.
	 * "type" is the tag's type (Tartist, ...) or Tunknown if libtags doesn't know how to map a tag kind to
	 * any of these. "k" is the raw key like "TPE1", "TPE2", etc. "s" is the null-terminated string unless "type" is
	 * Timage. "offset" and "size" define the placement and size of the image cover ("type" = Timage)
	 * inside the file, and "f" is not NULL in case reading the image cover requires additional
	 * operations on the data, in which case you need to read the image cover as a stream and call this
	 * function to apply these operations on the contents read.
	 */
	void (*tag)(Tagctx *ctx, int type, const char *k, const char *s, int offset, int size, Tagread f);

	/* Approximate millisecond-to-byte offsets within the file, if available. This callback is optional. */
	void (*toc)(Tagctx *ctx, int ms, int offset);

	/* Auxiliary data. Not used by libtags. */
	void *aux;

	/* Memory buffer to work in. */
	char *buf;

	/* Size of the buffer. Must be at least 256 bytes. */
	int bufsz;

	/* Here goes the stuff libtags sets. It should be accessed after tagsget() returns.
	 * A value of 0 means it's undefined.
	 */
	int channels; /* Number of channels. */
	int samplerate; /* Hz */
	int bitrate; /* Bitrate, bits/s. */
	int duration; /* ms */
	int format; /* Fmp3, Fvorbis, Fflac, Fm4a */

	/* Private, don't touch. */
	int found;
	int num;
	int restart;
};

/* Parse the file using this function. Returns 0 on success. */
extern int tagsget(Tagctx *ctx);
