/* i_sound.c */

#include "i_system.h"
#include "i_sound.h"
#include "w_wad.h"	// W_GetNumForName()
#include "z_zone.h"

/* The number of internal mixing channels,
**  the samples calculated for each mixing step,
**  the size of the 16bit, 2 hardware channel (stereo)
**  mixing buffer, and the samplerate of the raw data.
*/

/* Needed for calling the actual sound output. */
#define	SAMPLECOUNT	(512<<2)
#define	NUM_CHANNELS	8

/* The actual lengths of all sound effects. */
int	lengths[NUMSFX];

/* The actual output device. */
static int audio_fd;

/* The global mixing buffer.
** Basically, samples from all active internal channels
**  are modified and added, and stored in the buffer
**  that is submitted to the audio device.
*/
signed short	mixbuffer[SAMPLECOUNT*2];

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

void I_InitSound(void)
{
	int i;

	audio_fd = open("/dev/audio", OWRITE);
	if(audio_fd < 0) 
		printf("WARN Failed to open /dev/audio, sound disabled\n");

	/* Initialize external data (all sounds) at start, keep static. */
	for (i=1 ; i<NUMSFX ; i++)
	{
		/* Alias? Example is the chaingun sound linked to pistol. */
		if (!S_sfx[i].link)
		{
			/* Load data from WAD file. */
			S_sfx[i].data = getsfx( S_sfx[i].name, &lengths[i] );
		}
		else
		{
			/* Previously loaded already? */
			S_sfx[i].data = S_sfx[i].link->data;
			lengths[i] = lengths[(S_sfx[i].link - S_sfx)/sizeof(sfxinfo_t)];
		}
	}

	/* Now initialize mixbuffer with zero. */
	memset(mixbuffer, 0, sizeof mixbuffer);
}

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
void I_UpdateSound(void)
{
	/* Mix current sound data.
	** Data, from raw sound, for right and left.
	*/
	register uint	sample;
	register int	dl;
	register int	dr;

	/* Pointers in global mixbuffer, left, right, end. */
	signed short	*leftout;
	signed short	*rightout;
	signed short	*leftend;
	/* Step in mixbuffer, left and right, thus two. */
	int		step;

	/* Mixing channel index. */
	int		chan;

	/* Left and right channel
	**  are in global mixbuffer, alternating. */
	leftout = mixbuffer;
	rightout = mixbuffer+1;
	step = 2;

	/* Determine end, for left channel only
	**  (right channel is implicit). */
	leftend = mixbuffer + SAMPLECOUNT*step;

	/* Mix sounds into the mixing buffer.
	** Loop over step*SAMPLECOUNT
	**   that is 512 values for two channels.
	*/
	while (leftout != leftend)
	{
		/* Reset left/right value. */
		dl = 0;
		dr = 0;

		/* Love thy L2 cache - made this a loop.
		** Now more channels could be set at compile time
		**  as well. Thus loop those channels.
		*/
		for(chan=0; chan < NUM_CHANNELS; chan++)
		{
			/* Check channel, if active. */
			if(channels[ chan ])
			{
				/* Get the raw data from the channel. */
				sample = *channels[ chan ];
				/* Add left and right part
				**  for this channel (sound)
				**  to the current data.
				** Adjust volume accordingly.
				*/
				dl += channelleftvol_lookup[ chan ][sample];
				dr += channelrightvol_lookup[ chan ][sample];
				/* Increment index ??? */
				channelstepremainder[ chan ] += channelstep[ chan ];
				/* MSB is next sample??? */
				channels[ chan ] += channelstepremainder[ chan ] >> 16;
				/* Limit to LSB??? */
				channelstepremainder[ chan ] &= 65536-1;

				/* Check whether we are done. */
				if (channels[ chan ] >= channelsend[ chan ])
					channels[ chan ] = 0;
			}
		}

		/* Clamp to range. */
		if (dl > 0x7fff)
			dl = 0x7fff;
		else if (dl < -0x8000)
			dl = -0x8000;
		if (dr > 0x7fff)
			dr = 0x7fff;
		else if (dr < -0x8000)
			dr = -0x8000;

		*leftout = dl;
		*rightout = dr;

		/* Increment current pointers in mixbuffer. */
		leftout += step;
		rightout += step;

		*leftout = dl;
		*rightout = dr;
		leftout += step;
		rightout += step;

		*leftout = dl;
		*rightout = dr;
		leftout += step;
		rightout += step;

		*leftout = dl;
		*rightout = dr;
		leftout += step;
		rightout += step;
	}
}

void I_SubmitSound(void)
{
	if(audio_fd >= 0)
		write(audio_fd, mixbuffer, sizeof mixbuffer);
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

/* This function adds a sound to the
**  list of currently active sounds,
**  which is maintained as a given number
**  (eight, usually) of internal channels.
** eturns a handle.
*/
static int
addsfx(int id, int vol, int step, int sep)
{
	static unsigned short	handlenums = 0;
	int			i;
	int			rc;
	int			oldest = gametic;
	int			oldestnum = 0;
	int			slot;
	int			rightvol;
	int			leftvol;

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

	/* Okay, in the less recent channel,
	**  we will handle the new SFX.
	** Set pointer to raw data.
	*/
	channels[slot] = (uchar*) S_sfx[id].data;
	/* Set pointer to end of raw data. */
	channelsend[slot] = channels[slot] + lengths[id];

	/* Reset current handle number, limited to 0..100. */
	if (!handlenums)
		handlenums = 100;

	/* Assign current handle number.
	** Preserved so sounds could be stopped (unused).
	*/
	channelhandles[slot] = rc = handlenums++;

	/* Set stepping???
	** Kinda getting the impression this is never used.
	*/
	channelstep[slot] = step;
	/* ??? */
	channelstepremainder[slot] = 0;
	/* Should be gametic, I presume. */
	channelstart[slot] = gametic;

	/* Separation, that is, orientation/stereo.
	** range is : 1 - 256
	*/
	sep += 1;

	/* Per left/right channel.
	**  x^2 seperation,
	**  adjust volume properly.
	*/
	leftvol = vol - ((vol*sep*sep) >> 16);	// /(256*256);
	sep = sep - 257;
	rightvol = vol - ((vol*sep*sep) >> 16);

	/* Sanity check, clamp volume. */
	if (rightvol < 0 || rightvol > 127)
		I_Error("rightvol out of bounds");
	if (leftvol < 0 || leftvol > 127)
		I_Error("leftvol out of bounds");

	/* Get the proper lookup table piece
	**  for this volume level???
	*/
	channelleftvol_lookup[slot] = &vol_lookup[leftvol*256];
	channelrightvol_lookup[slot] = &vol_lookup[rightvol*256];

	/* Preserve sound SFX id,
	**  e.g. for avoiding duplicates of chainsaw.
	*/
	channelids[slot] = id;

	/* You tell me. */
	return rc;
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
	USED(priority);
	id = addsfx(id, vol, steptable[pitch], sep);
	return id;
}

void I_StopSound(int handle)
{
	USED(handle);
//	printf("PORTME i_sound.c I_StopSound\n");
}

int I_SoundIsPlaying(int handle)
{
	/* Ouch. */
	return gametic < handle;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
	/* I fail to see that this is used.
	** Would be using the handle to identify
	**  on which channel the sound might be active,
	**  and resetting the channel parameters.
	*/
	USED(handle, vol, sep, pitch);
}

void I_InitMusic(void)
{
//	printf("PORTME i_sound.c I_InitMusic\n");
}

void I_ShutdownMusic(void)
{
//	printf("PORTME i_sound.c I_ShutdownMusic\n");
}

void I_SetMusicVolume(int volume)
{
	USED(volume);
//	printf("PORTME i_sound.c I_SetMusicVolume\n");
}

void I_PauseSong(int handle)
{
	USED(handle);
//	printf("PORTME i_sound.c I_PauseSong\n");
}

void I_ResumeSong(int handle)
{
	USED(handle);
//	printf("PORTME i_sound.c I_ResumeSong\n");
}

int I_RegisterSong(void *data)
{
	USED(data);
//	printf("PORTME i_sound.c I_RegisterSong\n");
	return 0;
}

void I_PlaySong(int handle, int looping)
{
	USED(handle, looping);
//	printf("PORTME i_sound.c I_PlaySong\n");
}

void I_StopSong(int handle)
{
	USED(handle);
//	printf("PORTME i_sound.c I_StopSong\n");
}

void I_UnRegisterSong(int handle)
{
	USED(handle);
//	printf("PORTME i_sound.c I_UnregisterSong\n");
}
