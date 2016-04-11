/* Copyright (C) 2001-2012 Artifex Software, Inc.
   All Rights Reserved.

   This software is provided AS-IS with no warranty, either express or
   implied.

   This software is distributed under license and may not be copied,
   modified or distributed except as expressly authorized under the terms
   of the license contained in the file LICENSE in this distribution.

   Refer to licensing information at http://www.artifex.com or contact
   Artifex Software, Inc.,  7 Mt. Lassen Drive - Suite A-134, San Rafael,
   CA  94903, U.S.A., +1(415)492-9861, for further information.
*/



/* AES filter implementation */

#include "memory_.h"
#include "gserrors.h"
#include "gserror.h"
#include "strimpl.h"
#include "saes.h"

/* stream implementation */

private_st_aes_state();	/* creates a gc object for our state,
                           defined in saes.h */

/* Store a key in our crypt context */
int
s_aes_set_key(stream_aes_state * state, const unsigned char *key,
                  int keylength)
{
    if ( (keylength < 1) || (keylength > SAES_MAX_KEYLENGTH) )
        return_error(gs_error_rangecheck);
    if (key == NULL)
        return_error(gs_error_invalidaccess);

    /* we can't set the key here because the interpreter's
       filter implementation wants to duplicate our state
       after the zfaes.c binding calls us. So stash it now
       and handle it in our process method. */
    memcpy(state->key, key, keylength);
    state->keylength = keylength;

    /* return successfully */
    return 0;
}

/* Specify whether the plaintext stream uses RFC 1423-style padding
 * (allowing it to be an arbitrary length), or is unpadded (and must
 * therefore be a multiple of 16 bytes long). */
void
s_aes_set_padding(stream_aes_state *state, int use_padding)
{
    state->use_padding = use_padding;
}

/* initialize our state object. */
static int
s_aes_init(stream_state *ss)
{
    stream_aes_state *const state = (stream_aes_state *) ss;

    /* clear the flags so we know we're at the start of a stream */
    state->initialized = 0;
    memset(&state->aes, 0, sizeof(state->aes));

    return 0;
}

/* release our private storage */
static void
s_aes_release(stream_state *)
{
}

/* (de)crypt a section of text--the procedure is the same
 * in each direction. see strimpl.h for return codes.
 */
static int
s_aes_process(stream_state * ss, stream_cursor_read * pr,
                  stream_cursor_write * pw, bool last)
{
    stream_aes_state *const state = (stream_aes_state *) ss;
    const unsigned char *limit;
    const long in_size = pr->limit - pr->ptr;
    const long out_size = pw->limit - pw->ptr;
    unsigned char temp[16];
    int status = 0;

    /* figure out if we're going to run out of space */
    if (in_size > out_size) {
        limit = pr->ptr + out_size;
        status = 1; /* need more output space */
    } else {
        limit = pr->limit;
        status = last ? EOFC : 0; /* need more input */
    }
    if (state->keylength < 1 || state->keylength > SAES_MAX_KEYLENGTH)
        return ERRC;
    if (!state->initialized) {
        if (in_size < 16) return 0; /* get more data */

        /* read the initialization vector from the first 16 bytes */
        memcpy(state->iv, pr->ptr + 1, 16);
        pr->ptr += 16;

        setupAESstate(&state->aes, state->key, state->keylength, state->iv);
        state->initialized = 1;
    }

    /* decrypt available blocks */
    while (pr->ptr + 16 <= limit) {
      memcpy(temp, pr->ptr + 1, 16);
      aesCBCdecrypt(temp, 16, &state->aes);
      pr->ptr += 16;
      if (last && pr->ptr == pr->limit) {
        /* we're on the last block; unpad if necessary */
        int pad;

        if (state->use_padding) {
          /* we are using RFC 1423-style padding, so the last byte of the
             plaintext gives the number of bytes to discard */
          pad = temp[15];
          if (pad < 1 || pad > 16) {
            /* Bug 692343 - don't error here, just warn. Take padding to be
             * zero. This may give us a stream that's too long - preferable
             * to the alternatives. */
            pad = 0;
          }
        } else {
          /* not using padding */
          pad = 0;
        }

        memcpy(pw->ptr + 1, temp, 16 - pad);
        pw->ptr +=  16 - pad;
        return EOFC;
      }
      memcpy(pw->ptr + 1, temp, 16);
      pw->ptr += 16;
    }

    /* if we got to the end of the file without triggering the padding
       check, the input must not have been a multiple of 16 bytes long.
       complain. */
    if (status == EOFC)
      return 0;

    return status;
}

/* stream template */
const stream_template s_aes_template = {
    &st_aes_state, s_aes_init,
    s_aes_process, 16, 16,
    s_aes_release
};
