/* i_sound.c */

#include "i_system.h"
#include "i_sound.h"
#include "w_wad.h"	// W_GetNumForName()
#include "z_zone.h"
#include "m_argv.h"
#include <thread.h>

/* The number of internal mixing channels,
**  the samples calculated for each mixing step,
**  the size of the 16bit, 2 hardware channel (stereo)
**  mixing buffer, and the samplerate of the raw data.
*/

/* Needed for calling the actual sound output. */
#define	AUDFREQ		44100
#define	SFXFREQ		11025
#define	SAMPLECOUNT	(AUDFREQ/TICRATE)
#define	NUM_CHANNELS	32

/* The actual lengths of all sound effects. */
int	lengths[NUMSFX];

/* The actual output device. */
static int audio_fd = -1;

/* The global mixing buffer.
** Basically, samples from all active internal channels
**  are modified and added, and stored in the buffer
**  that is submitted to the audio device.
*/
uchar mixbuf[SAMPLECOUNT*4];

/* The channel step amount... */
uint	channelstep[NUM_CHANNELS];
/* ... and a 0.16 bit remainder of last step. */
uint	channelstepremainder[NUM_CHANNELS];

/* The channel data pointers, start and end. */
uchar*	channels[NUM_CHANNELS];
uchar*	channelsend[NUM_CHANNELS];

/* Time/gametic that the channel started playing,
**  used to determine oldest, which automatically
**  has lowest priority.
** In case number of active sounds exceeds
**  available channels.
*/
int	channelstart[NUM_CHANNELS];

/* The sound in channel handles,
**  determined on registration,
**  might be used to unregister/stop/modify,
**  currently unused.
*/
int	channelhandles[NUM_CHANNELS];
int lasthandle;

/* SFX id of the playing sound effect.
** Used to catch duplicates (like chainsaw).
*/
int	channelids[NUM_CHANNELS];

/* Pitch to stepping lookup, unused. */
int	steptable[256];

/* Volume lookups. */
int	vol_lookup[128*256];

/* Hardware left and right channel volume lookup. */
int*	channelleftvol_lookup[NUM_CHANNELS];
int*	channelrightvol_lookup[NUM_CHANNELS];

extern boolean mus_paused;

static int mpfd[2] = {-1, -1};

static void* getsfx(char *sfxname, int *len)
{
	uchar	*sfx;
	uchar	*paddedsfx;
	int	i;
	int	size;
	int	paddedsize;
	char	name[20];
	int	sfxlump;

	/* Get the sound data from the WAD, allocate lump
	**  in zone memory. */
	sprintf(name, "ds%s", sfxname);

	/* Now, there is a severe problem with the
	**  sound handling, in it is not (yet/anymore)
	**  gamemode aware. That means, sounds from
	**  DOOM II will be requested even with DOOM
	**  shareware.
	** The sound list is wired into sounds.c,
	**  which sets the external variable.
	** I do not do runtime patches to that
	**  variable. Instead, we will use a
	**  default sound for replacement.
	*/
	if ( W_CheckNumForName(name) == -1 )
		sfxlump = W_GetNumForName("dspistol");
	else
		sfxlump = W_GetNumForName(name);

	size = W_LumpLength( sfxlump );

	sfx = (uchar *)W_CacheLumpNum(sfxlump, PU_STATIC);

	/* Pads the sound effect out to the mixing buffer size.
	** The original realloc would interfere with zone memory.
	*/
	paddedsize = ((size-8 + (SAMPLECOUNT-1)) / SAMPLECOUNT) * SAMPLECOUNT;

	/* Allocate from zone memory. */
	paddedsfx = (uchar *)Z_Malloc(paddedsize+8, PU_STATIC, 0);

	/* Now copy and pad. */
	memcpy(paddedsfx, sfx, size);
	for (i=size ; i<paddedsize+8 ; i++)
		paddedsfx[i] = 128;

	/* Remove the cached lump. */
	Z_Free(sfx);

	/* Preserve padded length. */
	*len = paddedsize;

	/* Return allocated padded data. */
	return (void *)(paddedsfx + 8);
}

static void aproc(void *);

void I_InitSound(void)
{
	int i;

	audio_fd = open("/dev/audio", OWRITE);
	if(audio_fd < 0){
		fprint(2, "I_InitSound: disabling sound: %r\n");
		return;
	}
	/* Initialize external data (all sounds) at start, keep static. */
	for (i=1 ; i<NUMSFX ; i++)
	{
		if (!S_sfx[i].link)
		{
			/* Load data from WAD file. */
			S_sfx[i].data = getsfx( S_sfx[i].name, &lengths[i] );
		}
	}
	/* Alias? Example is the chaingun sound linked to pistol. */
	for (i=1 ; i<NUMSFX ; i++)
	{
		if (S_sfx[i].link)
		{
			/* Previously loaded already? */
			S_sfx[i].data = S_sfx[i].link->data;
			lengths[i] = lengths[S_sfx[i].link - S_sfx];
		}
	}
	proccreate(aproc, nil, 4096);
}

static QLock audlock;

/* This function loops all active (internal) sound
**  channels, retrieves a given number of samples
**  from the raw sound data, modifies it according
**  to the current (internal) channel parameters,
**  mixes the per-channel samples into the global
**  mixbuffer, clamping it to the allowed range,
**  and sets up everything for transferring the
**  contents of the mixbuffer to the (two)
**  hardware channels (left and right, that is).
**
** This function currently supports only 16bit.
*/
static int
soundtick(void)
{
	int l, r, i, v;
	uchar *p;

	memset(mixbuf, 0, sizeof mixbuf);
	if(mpfd[0]>=0 && !mus_paused && readn(mpfd[0], mixbuf, sizeof mixbuf) < 0){
		fprint(2, "I_UpdateSound: disabling music: %r\n");
		I_ShutdownMusic();
	}
	p = mixbuf;
	qlock(&audlock);
	while(p < mixbuf + sizeof mixbuf){
		l = 0;
		r = 0;
		for(i=0; i<NUM_CHANNELS; i++){
			if(channels[i] == nil)
				continue;
			v = *channels[i];
			l += channelleftvol_lookup[i][v];
			r += channelrightvol_lookup[i][v];
			channelstepremainder[i] += channelstep[i];
			channels[i] += channelstepremainder[i] >> 16;
			channelstepremainder[i] &= 0xffff;
			if(channels[i] >= channelsend[i]){
				channels[i] = 0;
				channelhandles[i] = 0;
			}
		}
		for(i=0; i<AUDFREQ/SFXFREQ; i++, p+=4){
			v = (short)(p[1] << 8 | p[0]);
			v = v * snd_MusicVolume / 15;
			v += l;
			if(v > 0x7fff)
				v = 0x7fff;
			else if(v < -0x8000)
				v = -0x8000;
			p[0] = v;
			p[1] = v >> 8;

			v = (short)(p[3] << 8 | p[2]);
			v = v * snd_MusicVolume / 15;
			v += r;
			if(v > 0x7fff)
				v = 0x7fff;
			else if(v < -0x8000)
				v = -0x8000;
			p[2] = v;
			p[3] = v >> 8;
		}
	}
	qunlock(&audlock);
	if(snd_SfxVolume|snd_MusicVolume)
		return write(audio_fd, mixbuf, sizeof mixbuf);
	return 0;
}

static void
aproc(void *)
{
	for(;;){
		if(soundtick() < 0)
			break;
	}
	threadexits(nil);
}

void I_UpdateSound(void)
{
}

void I_ShutdownSound(void)
{
	if(audio_fd >= 0) {
		close(audio_fd);
		audio_fd = -1;
	}
}

void I_SetChannels(void)
{
	/* Init internal lookups (raw data, mixing buffer, channels).
	** This function sets up internal lookups used during
	**  the mixing process.
	*/
	int	i;
	int	j;

	int	*steptablemid = steptable + 128;

	/* This table provides step widths for pitch parameters.
	** I fail to see that this is currently used. */
	for (i=-128 ; i<128 ; i++)
		steptablemid[i] = (int)(pow(2.0, (i/64.0))*65536.0);

	/* Generates volume lookup tables
	**  which also turn the unsigned samples
	**  into signed samples.
	*/
	for (i=0 ; i<128 ; i++)
		for (j=0 ; j<256 ; j++)
			vol_lookup[i*256+j] = (i*(j-128)*256)/127;
}

int I_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
	char namebuf[9];
	sprintf(namebuf, "ds%s", sfxinfo->name);
	return W_GetNumForName(namebuf);
}

static void setparams(int slot, int vol, int sep)
{
	int leftvol, rightvol;

	/* Separation, that is, orientation/stereo.
	** range is : 1 - 256
	*/
	sep += 1;
	/* Per left/right channel.
	**  x^2 seperation,
	**  adjust volume properly.
	*/
	leftvol = vol - ((vol*sep*sep) >> 16);
	sep = sep - 257;
	rightvol = vol - ((vol*sep*sep) >> 16);
	if(rightvol < 0 || rightvol > 127)
		I_Error("rightvol out of bounds");
	if(leftvol < 0 || leftvol > 127)
		I_Error("leftvol out of bounds");
	channelleftvol_lookup[slot] = &vol_lookup[leftvol*256];
	channelrightvol_lookup[slot] = &vol_lookup[rightvol*256];
}

/* This function adds a sound to the
**  list of currently active sounds,
**  which is maintained as a given number
**  (eight, usually) of internal channels.
** eturns a handle.
*/
static int
addsfx(int id, int vol, int step, int sep)
{
	int			i;
	int			rc;
	int			oldest = gametic;
	int			oldestnum = 0;
	int			slot;

	qlock(&audlock);
	/* Chainsaw troubles.
	** Play these sound effects only one at a time. */
	if ( id == sfx_sawup ||
	     id == sfx_sawidl ||
	     id == sfx_sawful ||
	     id == sfx_sawhit ||
	     id == sfx_stnmov ||
	     id == sfx_pistol )
	{
		/* Loop all channels, check. */
		for (i=0 ; i < NUM_CHANNELS ; i++)
		{
			/* Active and using the same SFX? */
			if( (channels[i]) && (channelids[i] == id) )
			{
				/* Reset. */
				channels[i] = 0;
				/* We are sure that iff,
				**  there will only be one. */
				break;
			}
		}
	}

	/* Loop all channels to find oldest SFX. */
	for (i=0 ; (i<NUM_CHANNELS) && (channels[i]) ; i++)
	{
		if(channelstart[i] < oldest)
		{
			oldestnum = i;
			oldest = channelstart[i];
		}
	}

	/* Tales from the cryptic.
	** If we found a channel, fine.
	** If not, we simply overwrite the first one, 0.
	** Probably only happens at startup.
	*/
	if (i == NUM_CHANNELS)
		slot = oldestnum;
	else
		slot = i;

	channels[slot] = (uchar*) S_sfx[id].data;
	channelsend[slot] = channels[slot] + lengths[id];
	channelstep[slot] = step;
	channelstepremainder[slot] = 0;
	channelstart[slot] = gametic;
	setparams(slot, vol, sep);
	channelids[slot] = id;
	channelhandles[slot] = rc = ++lasthandle;
	qunlock(&audlock);
	return rc;
}

int I_StartSound(int id, int vol, int sep, int pitch, int)
{
	if(audio_fd < 0)
		return -1;
	id = addsfx(id, vol, steptable[pitch], sep);
	return id;
}

void I_StopSound(int handle)
{
	int i;

	qlock(&audlock);
	for(i=0; i<NUM_CHANNELS; i++)
		if(channelhandles[i] == handle){
			channels[i] = 0;
			break;
		}
	qunlock(&audlock);
}

int I_SoundIsPlaying(int handle)
{
	int i, r;

	r = 0;
	qlock(&audlock);
	for(i=0; i<NUM_CHANNELS; i++)
		if(channelhandles[i] == handle){
			r = channels[i] != 0;
			break;
		}
	qunlock(&audlock);
	return r;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int /*pitch*/)
{
	int i, slot;

	qlock(&audlock);
	for(i=0, slot=0; i<NUM_CHANNELS; i++)
		if(channelhandles[i] == handle){
			slot = i;
			break;
		}
	if(i == NUM_CHANNELS){
		qunlock(&audlock);
		return;
	}
	setparams(slot, vol, sep);
	qunlock(&audlock);
}

void I_ShutdownMusic(void)
{
	if(mpfd[0] >= 0){
		close(mpfd[0]);
		mpfd[0] = -1;
		waitpid();
	}
}

void I_SetMusicVolume(int)
{
}

void I_PauseSong(int)
{
}

void I_ResumeSong(int)
{
}

void I_PlaySong(musicinfo_t *m, int loop)
{
	char name[64];
	int n;

	if(M_CheckParm("-nomusic") || audio_fd < 0)
		return;
	I_ShutdownMusic();
	if(pipe(mpfd) < 0)
		return;
	switch(rfork(RFPROC|RFFDG|RFNAMEG)){
	case -1:
		fprint(2, "I_PlaySong: %r\n");
		break;
	case 0:
		dup(mpfd[1], 1);
		for(n=3; n<20; n++) close(n);
		close(0);
		snprint(name, sizeof(name), "/mnt/wad/d_%s", m->name);
		if(bind("/fd/1", "/dev/audio", MREPL) == -1)
			sysfatal("bind: %r");
		while(loop && fork() > 0){
			if(waitpid() < 0 || write(1, "", 0) < 0)
				exits(nil);
		}
		execl("/bin/dmus", "dmus", name, m->name, nil);
		execl("/bin/play", "play", name, nil);
		sysfatal("execl: %r");
	default:
		close(mpfd[1]);
	}
}

void I_StopSong(int)
{
	I_ShutdownMusic();
}
