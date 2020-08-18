/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggVorbis SOFTWARE CODEC SOURCE CODE.   *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE OggVorbis SOURCE CODE IS (C) COPYRIGHT 1994-2002             *
 * by the XIPHOPHORUS Company http://www.xiph.org/                  *
 *                                                                  *
 ********************************************************************

 function: simple example decoder
 last mod: $Id: decoder_example.c,v 1.27 2002/07/12 15:07:52 giles Exp $

 ********************************************************************/

/* Takes a vorbis bitstream from stdin and writes raw stereo PCM to
   stdout.  Decodes simple and chained OggVorbis files from beginning
   to end.  Vorbisfile.a is somewhat more complex than the code below.  */

/* Note that this is POSIX, not ANSI code */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <vorbis/codec.h>
#include <sys/wait.h>
#define _PLAN9_SOURCE
#include <utf.h>
#include <lib9.h>

static int ifd = -1;

static void
output(float **pcm, int samples, vorbis_info *vi)
{
	static int rate, chans;
	static unsigned char *buf;
	static int nbuf;
	unsigned char *p;
	int i, j, n, v, status;
	float *s;

	/* start converter if format changed */
	if(rate != vi->rate || chans != vi->channels){
		int pid, pfd[2];
		char fmt[32];

		rate = vi->rate;
		chans = vi->channels;
		sprintf(fmt, "f%dr%dc%d", sizeof(float)*8, rate, chans);

		if(ifd >= 0){
			close(ifd);
			wait(&status);
		}
		if(pipe(pfd) < 0){
			fprintf(stderr, "Error creating pipe\n");
			exit(1);
		}
		pid = fork();
		if(pid < 0){
			fprintf(stderr, "Error forking\n");
			exit(1);
		}
		if(pid == 0){
			dup2(pfd[1], 0);
			close(pfd[1]);
			close(pfd[0]);
			execl("/bin/audio/pcmconv", "pcmconv", "-i", fmt, NULL);
			fprintf(stderr, "Error executing converter\n");
			exit(1);
		}
		close(pfd[1]);
		ifd = pfd[0];
	}
	n = sizeof(float) * chans * samples;
	if(n > nbuf){
		nbuf = n;
		buf = realloc(buf, nbuf);
		if(buf == NULL){
			fprintf(stderr, "Error allocating memory\n");
			exit(1);
		}
	}
	p = buf;
	for(j=0; j < chans; j++){
		s = pcm[j];
		p = buf + j*sizeof(float);
		for(i=0; i < samples; i++){
			*((float*)p) = *s++;
			p += chans*sizeof(float);
		}
	}
	if(n > 0)
		write(ifd, buf, n);
}

void
usage(void)
{
	fprintf(stderr, "usage: %s [-s SECONDS]\n", argv0);
	exit(1);
}

int main(int argc, char **argv){
  ogg_sync_state   oy; /* sync and verify incoming physical bitstream */
  ogg_stream_state os; /* take physical pages, weld into a logical
			  stream of packets */
  ogg_page         og; /* one Ogg bitstream page.  Vorbis packets are inside */
  ogg_packet       op; /* one raw packet of data for decode */
  
  vorbis_info      vi; /* struct that stores all the static vorbis bitstream
			  settings */
  vorbis_comment   vc; /* struct that stores all the bitstream user comments */
  vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
  vorbis_block     vb; /* local working space for packet->PCM decode */
  
  char *buffer;
  int  bytes;
  int  status, n;

  long   seeking, seekoff = 4096;
  double seek, left, right, time, lasttime;

  seek = 0.0;
  ARGBEGIN{
  case 's':
  	seek = atof(EARGF(usage()));
  	seeking = 1;
  	if(seek >= 0.0)
  	  break;
  default:
    usage();
  }ARGEND;

  lasttime = left = 0.0;
  right = seek;

  /********** Decode setup ************/

  ogg_sync_init(&oy); /* Now we can read pages */
  
  while(1){ /* we repeat if the bitstream is chained */
    int eos=0;
    int i;

    /* grab some data at the head of the stream.  We want the first page
       (which is guaranteed to be small and only contain the Vorbis
       stream initial header) We need the first page to get the stream
       serialno. */

    /* submit a 4k block to libvorbis' Ogg layer */
    buffer=ogg_sync_buffer(&oy,4096);
    bytes=fread(buffer,1,4096,stdin);
    ogg_sync_wrote(&oy,bytes);
    
    /* Get the first page. */
    if(ogg_sync_pageout(&oy,&og)!=1){
      /* have we simply run out of data?  If so, we're done. */
      if(bytes<4096)break;
      /* error case.  Must not be Vorbis data */
      fprintf(stderr,"Input does not appear to be an Ogg bitstream.\n");
      exit(1);
    }

    
BOS:/* Begin of stream */

    /* Get the serial number and set up the rest of decode. */
    /* serialno first; use it to set up a logical stream */
    ogg_stream_init(&os,ogg_page_serialno(&og));
    
    /* extract the initial header from the first page and verify that the
       Ogg bitstream is in fact Vorbis data */
    
    /* I handle the initial header first instead of just having the code
       read all three Vorbis headers at once because reading the initial
       header is an easy way to identify a Vorbis bitstream and it's
       useful to see that functionality seperated out. */
    
    vorbis_info_init(&vi);
    vorbis_comment_init(&vc);
    if(ogg_stream_pagein(&os,&og)<0){ 
      /* error; stream version mismatch perhaps */
      fprintf(stderr,"Error reading first page of Ogg bitstream data.\n");
      exit(1);
    }
    
    if(ogg_stream_packetout(&os,&op)!=1){ 
      /* no page? must not be vorbis */
      fprintf(stderr,"Error reading initial header packet.\n");
      exit(1);
    }
    
    if(vorbis_synthesis_headerin(&vi,&vc,&op)<0){ 
      /* error case; not a vorbis header */
      fprintf(stderr,"This Ogg bitstream does not contain Vorbis "
	      "audio data.\n");
      exit(1);
    }
    
    /* At this point, we're sure we're Vorbis.  We've set up the logical
       (Ogg) bitstream decoder.  Get the comment and codebook headers and
       set up the Vorbis decoder */
    
    /* The next two packets in order are the comment and codebook headers.
       They're likely large and may span multiple pages.  Thus we reead
       and submit data until we get our two pacakets, watching that no
       pages are missing.  If a page is missing, error out; losing a
       header page is the only place where missing data is fatal. */
    
    i=0;
    while(i<2){
      while(i<2){
	int result=ogg_sync_pageout(&oy,&og);
	if(result==0)break; /* Need more data */
	/* Don't complain about missing or corrupt data yet.  We'll
	   catch it at the packet output phase */
	if(result==1){
	  ogg_stream_pagein(&os,&og); /* we can ignore any errors here
					 as they'll also become apparent
					 at packetout */
	  while(i<2){
	    result=ogg_stream_packetout(&os,&op);
	    if(result==0)break;
	    if(result<0){
	      /* Uh oh; data at some point was corrupted or missing!
		 We can't tolerate that in a header.  Die. */
	      fprintf(stderr,"Corrupt secondary header.  Exiting.\n");
	      exit(1);
	    }
	    vorbis_synthesis_headerin(&vi,&vc,&op);
	    i++;
	  }
	}
      }
      /* no harm in not checking before adding more */
      buffer=ogg_sync_buffer(&oy,4096);
      bytes=fread(buffer,1,4096,stdin);
      if(bytes==0 && i<2){
	fprintf(stderr,"End of file before finding all Vorbis headers!\n");
	exit(1);
      }
      ogg_sync_wrote(&oy,bytes);
    }
    
    /* Throw the comments plus a few lines about the bitstream we're
       decoding */
    {
      char **ptr=vc.user_comments;
      while(*ptr){
	fprintf(stderr,"%s\n",*ptr);
	++ptr;
      }
      fprintf(stderr,"\nBitstream is %d channel, %ldHz\n",vi.channels,vi.rate);
      fprintf(stderr,"Encoded by: %s\n\n",vc.vendor);
    }
    
    /* OK, got and parsed all three headers. Initialize the Vorbis
       packet->PCM decoder. */
    vorbis_synthesis_init(&vd,&vi); /* central decode state */
    vorbis_block_init(&vd,&vb);     /* local state for most of the decode
				       so multiple block decodes can
				       proceed in parallel.  We could init
				       multiple vorbis_block structures
				       for vd here */

    /* The rest is just a straight decode loop until end of stream */
    while(!eos){
      while(!eos){
	int result=ogg_sync_pageout(&oy,&og);
	if(result==0)break; /* need more data */
	if(result<0){ /* missing or corrupt data at this page position */
	  if(!seeking)
	    fprintf(stderr,"Corrupt or missing data in bitstream; "
		  "continuing...\n");
	}else{
	  if(ogg_page_bos(&og)){ /* got new start of stream */
	    ogg_stream_clear(&os);
	    vorbis_block_clear(&vb);
	    vorbis_dsp_clear(&vd);
	    vorbis_comment_clear(&vc);
	    vorbis_info_clear(&vi);
	    goto BOS;
	  }
	  ogg_stream_pagein(&os,&og); /* can safely ignore errors at
					 this point */

	  while(1){
	    result=ogg_stream_packetout(&os,&op);

	    if(result==0)break; /* need more data */
	    if(result<0){ /* missing or corrupt data at this page position */
	      /* no reason to complain; already complained above */
	    }else{
	      if(seeking){
	        time = vorbis_granule_time(&vd, ogg_page_granulepos(&og));
	        if(time > left && time < right && (time - lasttime > 0.1 || lasttime < 0.1)){
	          if(time > seek)
	            right = time;
	          else
	            left = time;
    	      seekoff *= (seek - time)/(time - lasttime);
	          if(fabs(right - left) > 1.0 && seekoff >= 4096){
	            lasttime = time;
                if((n = fseek(stdin, seekoff, SEEK_CUR)) < 0 && feof(stdin))
                  n = fseek(stdin, -seekoff/2, SEEK_END);
                if(n >= 0){
	      	      ogg_sync_reset(&oy);
	              ogg_stream_reset(&os);
                  goto again;
                }
              }
            }
            seeking = 0;
            fprintf(stderr, "time: %g\n", time);
          }
	      float **pcm;
	      int samples;
	      if(vorbis_synthesis(&vb,&op)==0) /* test for success! */
		vorbis_synthesis_blockin(&vd,&vb);
	      while((samples=vorbis_synthesis_pcmout(&vd,&pcm))>0){
		output(pcm, samples, &vi);
		vorbis_synthesis_read(&vd,samples); /* tell libvorbis how
						   many samples we
						   actually consumed */
	      }
	    }
	  }
	  if(ogg_page_eos(&og))eos=1;
	}
      }
      if(!eos){
again:
	buffer=ogg_sync_buffer(&oy,4096);
	bytes=fread(buffer,1,4096,stdin);
	ogg_sync_wrote(&oy,bytes);
	if(bytes==0)eos=1;
      }
    }
    
    /* clean up this logical bitstream; before exit we see if we're
       followed by another [chained] */

    ogg_stream_clear(&os);
  
    /* ogg_page and ogg_packet structs always point to storage in
       libvorbis.  They're never freed or manipulated directly */
    
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
	vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);  /* must be called last */
  }

  /* OK, clean up the framer */
  ogg_sync_clear(&oy);

  if(ifd >= 0){
    close(ifd);
    wait(&status);
  }

  fprintf(stderr,"Done.\n");
  return(0);
}
