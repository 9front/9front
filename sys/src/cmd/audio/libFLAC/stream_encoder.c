/* libFLAC - Free Lossless Audio Codec library
 * Copyright (C) 2000,2001,2002,2003,2004  Josh Coalson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of the Xiph.org Foundation nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h> /* for malloc() */
#include <string.h> /* for memcpy() */
#include "FLAC/assert.h"
#include "FLAC/stream_decoder.h"
#include "protected/stream_encoder.h"
#include "private/bitbuffer.h"
#include "private/bitmath.h"
#include "private/crc.h"
#include "private/cpu.h"
#include "private/fixed.h"
#include "private/format.h"
#include "private/lpc.h"
#include "private/md5.h"
#include "private/memory.h"
#include "private/stream_encoder_framing.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef min
#undef min
#endif
#define min(x,y) ((x)<(y)?(x):(y))

#ifdef max
#undef max
#endif
#define max(x,y) ((x)>(y)?(x):(y))

typedef struct {
	FLAC__int32 *data[FLAC__MAX_CHANNELS];
	unsigned size; /* of each data[] in samples */
	unsigned tail;
} verify_input_fifo;

typedef struct {
	const FLAC__byte *data;
	unsigned capacity;
	unsigned bytes;
} verify_output;

typedef enum {
	ENCODER_IN_MAGIC = 0,
	ENCODER_IN_METADATA = 1,
	ENCODER_IN_AUDIO = 2
} EncoderStateHint;

/***********************************************************************
 *
 * Private class method prototypes
 *
 ***********************************************************************/

static void set_defaults_(FLAC__StreamEncoder *encoder);
static void free_(FLAC__StreamEncoder *encoder);
static FLAC__bool resize_buffers_(FLAC__StreamEncoder *encoder, unsigned new_size);
static FLAC__bool write_bitbuffer_(FLAC__StreamEncoder *encoder, unsigned samples);
static FLAC__bool process_frame_(FLAC__StreamEncoder *encoder, FLAC__bool is_last_frame);
static FLAC__bool process_subframes_(FLAC__StreamEncoder *encoder, FLAC__bool is_last_frame);

static FLAC__bool process_subframe_(
	FLAC__StreamEncoder *encoder,
	unsigned min_partition_order,
	unsigned max_partition_order,
	FLAC__bool precompute_partition_sums,
	const FLAC__FrameHeader *frame_header,
	unsigned subframe_bps,
	const FLAC__int32 integer_signal[],
	const FLAC__real real_signal[],
	FLAC__Subframe *subframe[2],
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents[2],
	FLAC__int32 *residual[2],
	unsigned *best_subframe,
	unsigned *best_bits
);

static FLAC__bool add_subframe_(
	FLAC__StreamEncoder *encoder,
	const FLAC__FrameHeader *frame_header,
	unsigned subframe_bps,
	const FLAC__Subframe *subframe,
	FLAC__BitBuffer *frame
);

static unsigned evaluate_constant_subframe_(
	const FLAC__int32 signal,
	unsigned subframe_bps,
	FLAC__Subframe *subframe
);

static unsigned evaluate_fixed_subframe_(
	FLAC__StreamEncoder *encoder,
	const FLAC__int32 signal[],
	FLAC__int32 residual[],
	FLAC__uint32 abs_residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned raw_bits_per_partition[],
	unsigned blocksize,
	unsigned subframe_bps,
	unsigned order,
	unsigned rice_parameter,
	unsigned min_partition_order,
	unsigned max_partition_order,
	FLAC__bool precompute_partition_sums,
	FLAC__bool do_escape_coding,
	unsigned rice_parameter_search_dist,
	FLAC__Subframe *subframe,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents
);

static unsigned evaluate_lpc_subframe_(
	FLAC__StreamEncoder *encoder,
	const FLAC__int32 signal[],
	FLAC__int32 residual[],
	FLAC__uint32 abs_residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned raw_bits_per_partition[],
	const FLAC__real lp_coeff[],
	unsigned blocksize,
	unsigned subframe_bps,
	unsigned order,
	unsigned qlp_coeff_precision,
	unsigned rice_parameter,
	unsigned min_partition_order,
	unsigned max_partition_order,
	FLAC__bool precompute_partition_sums,
	FLAC__bool do_escape_coding,
	unsigned rice_parameter_search_dist,
	FLAC__Subframe *subframe,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents
);

static unsigned evaluate_verbatim_subframe_(
	const FLAC__int32 signal[],
	unsigned blocksize,
	unsigned subframe_bps,
	FLAC__Subframe *subframe
);

static unsigned find_best_partition_order_(
	struct FLAC__StreamEncoderPrivate *private_,
	const FLAC__int32 residual[],
	FLAC__uint32 abs_residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned raw_bits_per_partition[],
	unsigned residual_samples,
	unsigned predictor_order,
	unsigned rice_parameter,
	unsigned min_partition_order,
	unsigned max_partition_order,
	FLAC__bool precompute_partition_sums,
	FLAC__bool do_escape_coding,
	unsigned rice_parameter_search_dist,
	FLAC__EntropyCodingMethod_PartitionedRice *best_partitioned_rice
);

static void precompute_partition_info_sums_(
	const FLAC__uint32 abs_residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned residual_samples,
	unsigned predictor_order,
	unsigned min_partition_order,
	unsigned max_partition_order
);

static void precompute_partition_info_escapes_(
	const FLAC__int32 residual[],
	unsigned raw_bits_per_partition[],
	unsigned residual_samples,
	unsigned predictor_order,
	unsigned min_partition_order,
	unsigned max_partition_order
);

#ifdef DONT_ESTIMATE_RICE_BITS
static FLAC__bool set_partitioned_rice_(
	const FLAC__uint32 abs_residual[],
	const FLAC__int32 residual[],
	const unsigned residual_samples,
	const unsigned predictor_order,
	const unsigned suggested_rice_parameter,
	const unsigned rice_parameter_search_dist,
	const unsigned partition_order,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents,
	unsigned *bits
);

static FLAC__bool set_partitioned_rice_with_precompute_(
	const FLAC__int32 residual[],
	const FLAC__uint64 abs_residual_partition_sums[],
	const unsigned raw_bits_per_partition[],
	const unsigned residual_samples,
	const unsigned predictor_order,
	const unsigned suggested_rice_parameter,
	const unsigned rice_parameter_search_dist,
	const unsigned partition_order,
	const FLAC__bool search_for_escapes,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents,
	unsigned *bits
);
#else
static FLAC__bool set_partitioned_rice_(
	const FLAC__uint32 abs_residual[],
	const unsigned residual_samples,
	const unsigned predictor_order,
	const unsigned suggested_rice_parameter,
	const unsigned rice_parameter_search_dist,
	const unsigned partition_order,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents,
	unsigned *bits
);

static FLAC__bool set_partitioned_rice_with_precompute_(
	const FLAC__uint32 abs_residual[],
	const FLAC__uint64 abs_residual_partition_sums[],
	const unsigned raw_bits_per_partition[],
	const unsigned residual_samples,
	const unsigned predictor_order,
	const unsigned suggested_rice_parameter,
	const unsigned rice_parameter_search_dist,
	const unsigned partition_order,
	const FLAC__bool search_for_escapes,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents,
	unsigned *bits
);
#endif

static unsigned get_wasted_bits_(FLAC__int32 signal[], unsigned samples);

/* verify-related routines: */
static void append_to_verify_fifo_(
	verify_input_fifo *fifo,
	const FLAC__int32 * const input[],
	unsigned input_offset,
	unsigned channels,
	unsigned wide_samples
);

static void append_to_verify_fifo_interleaved_(
	verify_input_fifo *fifo,
	const FLAC__int32 input[],
	unsigned input_offset,
	unsigned channels,
	unsigned wide_samples
);

static FLAC__StreamDecoderReadStatus verify_read_callback_(
	const FLAC__StreamDecoder *decoder,
	FLAC__byte buffer[],
	unsigned *bytes,
	void *client_data
);

static FLAC__StreamDecoderWriteStatus verify_write_callback_(
	const FLAC__StreamDecoder *decoder,
	const FLAC__Frame *frame,
	const FLAC__int32 * const buffer[],
	void *client_data
);

static void verify_metadata_callback_(
	const FLAC__StreamDecoder *decoder,
	const FLAC__StreamMetadata *metadata,
	void *client_data
);

static void verify_error_callback_(
	const FLAC__StreamDecoder *decoder,
	FLAC__StreamDecoderErrorStatus status,
	void *client_data
);


/***********************************************************************
 *
 * Private class data
 *
 ***********************************************************************/

typedef struct FLAC__StreamEncoderPrivate {
	unsigned input_capacity;                          /* current size (in samples) of the signal and residual buffers */
	FLAC__int32 *integer_signal[FLAC__MAX_CHANNELS];  /* the integer version of the input signal */
	FLAC__int32 *integer_signal_mid_side[2];          /* the integer version of the mid-side input signal (stereo only) */
	FLAC__real *real_signal[FLAC__MAX_CHANNELS];      /* the floating-point version of the input signal */
	FLAC__real *real_signal_mid_side[2];              /* the floating-point version of the mid-side input signal (stereo only) */
	unsigned subframe_bps[FLAC__MAX_CHANNELS];        /* the effective bits per sample of the input signal (stream bps - wasted bits) */
	unsigned subframe_bps_mid_side[2];                /* the effective bits per sample of the mid-side input signal (stream bps - wasted bits + 0/1) */
	FLAC__int32 *residual_workspace[FLAC__MAX_CHANNELS][2]; /* each channel has a candidate and best workspace where the subframe residual signals will be stored */
	FLAC__int32 *residual_workspace_mid_side[2][2];
	FLAC__Subframe subframe_workspace[FLAC__MAX_CHANNELS][2];
	FLAC__Subframe subframe_workspace_mid_side[2][2];
	FLAC__Subframe *subframe_workspace_ptr[FLAC__MAX_CHANNELS][2];
	FLAC__Subframe *subframe_workspace_ptr_mid_side[2][2];
	FLAC__EntropyCodingMethod_PartitionedRiceContents partitioned_rice_contents_workspace[FLAC__MAX_CHANNELS][2];
	FLAC__EntropyCodingMethod_PartitionedRiceContents partitioned_rice_contents_workspace_mid_side[FLAC__MAX_CHANNELS][2];
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents_workspace_ptr[FLAC__MAX_CHANNELS][2];
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents_workspace_ptr_mid_side[FLAC__MAX_CHANNELS][2];
	unsigned best_subframe[FLAC__MAX_CHANNELS];       /* index into the above workspaces */
	unsigned best_subframe_mid_side[2];
	unsigned best_subframe_bits[FLAC__MAX_CHANNELS];  /* size in bits of the best subframe for each channel */
	unsigned best_subframe_bits_mid_side[2];
	FLAC__uint32 *abs_residual;                       /* workspace where abs(candidate residual) is stored */
	FLAC__uint64 *abs_residual_partition_sums;        /* workspace where the sum of abs(candidate residual) for each partition is stored */
	unsigned *raw_bits_per_partition;                 /* workspace where the sum of silog2(candidate residual) for each partition is stored */
	FLAC__BitBuffer *frame;                           /* the current frame being worked on */
	double loose_mid_side_stereo_frames_exact;        /* exact number of frames the encoder will use before trying both independent and mid/side frames again */
	unsigned loose_mid_side_stereo_frames;            /* rounded number of frames the encoder will use before trying both independent and mid/side frames again */
	unsigned loose_mid_side_stereo_frame_count;       /* number of frames using the current channel assignment */
	FLAC__ChannelAssignment last_channel_assignment;
	FLAC__StreamMetadata metadata;
	unsigned current_sample_number;
	unsigned current_frame_number;
	struct FLAC__MD5Context md5context;
	FLAC__CPUInfo cpuinfo;
	unsigned (*local_fixed_compute_best_predictor)(const FLAC__int32 data[], unsigned data_len, FLAC__real residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
	void (*local_lpc_compute_autocorrelation)(const FLAC__real data[], unsigned data_len, unsigned lag, FLAC__real autoc[]);
	void (*local_lpc_compute_residual_from_qlp_coefficients)(const FLAC__int32 data[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 residual[]);
	void (*local_lpc_compute_residual_from_qlp_coefficients_64bit)(const FLAC__int32 data[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 residual[]);
	void (*local_lpc_compute_residual_from_qlp_coefficients_16bit)(const FLAC__int32 data[], unsigned data_len, const FLAC__int32 qlp_coeff[], unsigned order, int lp_quantization, FLAC__int32 residual[]);
	FLAC__bool use_wide_by_block;          /* use slow 64-bit versions of some functions because of the block size */
	FLAC__bool use_wide_by_partition;      /* use slow 64-bit versions of some functions because of the min partition order and blocksize */
	FLAC__bool use_wide_by_order;          /* use slow 64-bit versions of some functions because of the lpc order */
	FLAC__bool precompute_partition_sums;  /* our initial guess as to whether precomputing the partitions sums will be a speed improvement */
	FLAC__bool disable_constant_subframes;
	FLAC__bool disable_fixed_subframes;
	FLAC__bool disable_verbatim_subframes;
	FLAC__StreamEncoderWriteCallback write_callback;
	FLAC__StreamEncoderMetadataCallback metadata_callback;
	void *client_data;
	/* unaligned (original) pointers to allocated data */
	FLAC__int32 *integer_signal_unaligned[FLAC__MAX_CHANNELS];
	FLAC__int32 *integer_signal_mid_side_unaligned[2];
	FLAC__real *real_signal_unaligned[FLAC__MAX_CHANNELS];
	FLAC__real *real_signal_mid_side_unaligned[2];
	FLAC__int32 *residual_workspace_unaligned[FLAC__MAX_CHANNELS][2];
	FLAC__int32 *residual_workspace_mid_side_unaligned[2][2];
	FLAC__uint32 *abs_residual_unaligned;
	FLAC__uint64 *abs_residual_partition_sums_unaligned;
	unsigned *raw_bits_per_partition_unaligned;
	/*
	 * These fields have been moved here from private function local
	 * declarations merely to save stack space during encoding.
	 */
	FLAC__real lp_coeff[FLAC__MAX_LPC_ORDER][FLAC__MAX_LPC_ORDER]; /* from process_subframe_() */
	FLAC__EntropyCodingMethod_PartitionedRiceContents partitioned_rice_contents_extra[2]; /* from find_best_partition_order_() */
	/*
	 * The data for the verify section
	 */
	struct {
		FLAC__StreamDecoder *decoder;
		EncoderStateHint state_hint;
		FLAC__bool needs_magic_hack;
		verify_input_fifo input_fifo;
		verify_output output;
		struct {
			FLAC__uint64 absolute_sample;
			unsigned frame_number;
			unsigned channel;
			unsigned sample;
			FLAC__int32 expected;
			FLAC__int32 got;
		} error_stats;
	} verify;
	FLAC__bool is_being_deleted; /* if true, call to ..._finish() from ..._delete() will not call the callbacks */
} FLAC__StreamEncoderPrivate;

/***********************************************************************
 *
 * Public static class data
 *
 ***********************************************************************/

FLAC_API const char * const FLAC__StreamEncoderStateString[] = {
	"FLAC__STREAM_ENCODER_OK",
	"FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR",
	"FLAC__STREAM_ENCODER_VERIFY_MISMATCH_IN_AUDIO_DATA",
	"FLAC__STREAM_ENCODER_INVALID_CALLBACK",
	"FLAC__STREAM_ENCODER_INVALID_NUMBER_OF_CHANNELS",
	"FLAC__STREAM_ENCODER_INVALID_BITS_PER_SAMPLE",
	"FLAC__STREAM_ENCODER_INVALID_SAMPLE_RATE",
	"FLAC__STREAM_ENCODER_INVALID_BLOCK_SIZE",
	"FLAC__STREAM_ENCODER_INVALID_MAX_LPC_ORDER",
	"FLAC__STREAM_ENCODER_INVALID_QLP_COEFF_PRECISION",
	"FLAC__STREAM_ENCODER_MID_SIDE_CHANNELS_MISMATCH",
	"FLAC__STREAM_ENCODER_MID_SIDE_SAMPLE_SIZE_MISMATCH",
	"FLAC__STREAM_ENCODER_ILLEGAL_MID_SIDE_FORCE",
	"FLAC__STREAM_ENCODER_BLOCK_SIZE_TOO_SMALL_FOR_LPC_ORDER",
	"FLAC__STREAM_ENCODER_NOT_STREAMABLE",
	"FLAC__STREAM_ENCODER_FRAMING_ERROR",
	"FLAC__STREAM_ENCODER_INVALID_METADATA",
	"FLAC__STREAM_ENCODER_FATAL_ERROR_WHILE_ENCODING",
	"FLAC__STREAM_ENCODER_FATAL_ERROR_WHILE_WRITING",
	"FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR",
	"FLAC__STREAM_ENCODER_ALREADY_INITIALIZED",
	"FLAC__STREAM_ENCODER_UNINITIALIZED"
};

FLAC_API const char * const FLAC__StreamEncoderWriteStatusString[] = {
	"FLAC__STREAM_ENCODER_WRITE_STATUS_OK",
	"FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR"
};

/***********************************************************************
 *
 * Class constructor/destructor
 *
 */
FLAC_API FLAC__StreamEncoder *FLAC__stream_encoder_new()
{
	FLAC__StreamEncoder *encoder;
	unsigned i;

	FLAC__ASSERT(sizeof(int) >= 4); /* we want to die right away if this is not true */

	encoder = (FLAC__StreamEncoder*)calloc(1, sizeof(FLAC__StreamEncoder));
	if(encoder == 0) {
		return 0;
	}

	encoder->protected_ = (FLAC__StreamEncoderProtected*)calloc(1, sizeof(FLAC__StreamEncoderProtected));
	if(encoder->protected_ == 0) {
		free(encoder);
		return 0;
	}

	encoder->private_ = (FLAC__StreamEncoderPrivate*)calloc(1, sizeof(FLAC__StreamEncoderPrivate));
	if(encoder->private_ == 0) {
		free(encoder->protected_);
		free(encoder);
		return 0;
	}

	encoder->private_->frame = FLAC__bitbuffer_new();
	if(encoder->private_->frame == 0) {
		free(encoder->private_);
		free(encoder->protected_);
		free(encoder);
		return 0;
	}

	set_defaults_(encoder);

	encoder->private_->is_being_deleted = false;

	for(i = 0; i < FLAC__MAX_CHANNELS; i++) {
		encoder->private_->subframe_workspace_ptr[i][0] = &encoder->private_->subframe_workspace[i][0];
		encoder->private_->subframe_workspace_ptr[i][1] = &encoder->private_->subframe_workspace[i][1];
	}
	for(i = 0; i < 2; i++) {
		encoder->private_->subframe_workspace_ptr_mid_side[i][0] = &encoder->private_->subframe_workspace_mid_side[i][0];
		encoder->private_->subframe_workspace_ptr_mid_side[i][1] = &encoder->private_->subframe_workspace_mid_side[i][1];
	}
	for(i = 0; i < FLAC__MAX_CHANNELS; i++) {
		encoder->private_->partitioned_rice_contents_workspace_ptr[i][0] = &encoder->private_->partitioned_rice_contents_workspace[i][0];
		encoder->private_->partitioned_rice_contents_workspace_ptr[i][1] = &encoder->private_->partitioned_rice_contents_workspace[i][1];
	}
	for(i = 0; i < 2; i++) {
		encoder->private_->partitioned_rice_contents_workspace_ptr_mid_side[i][0] = &encoder->private_->partitioned_rice_contents_workspace_mid_side[i][0];
		encoder->private_->partitioned_rice_contents_workspace_ptr_mid_side[i][1] = &encoder->private_->partitioned_rice_contents_workspace_mid_side[i][1];
	}

	for(i = 0; i < FLAC__MAX_CHANNELS; i++) {
		FLAC__format_entropy_coding_method_partitioned_rice_contents_init(&encoder->private_->partitioned_rice_contents_workspace[i][0]);
		FLAC__format_entropy_coding_method_partitioned_rice_contents_init(&encoder->private_->partitioned_rice_contents_workspace[i][1]);
	}
	for(i = 0; i < 2; i++) {
		FLAC__format_entropy_coding_method_partitioned_rice_contents_init(&encoder->private_->partitioned_rice_contents_workspace_mid_side[i][0]);
		FLAC__format_entropy_coding_method_partitioned_rice_contents_init(&encoder->private_->partitioned_rice_contents_workspace_mid_side[i][1]);
	}
	for(i = 0; i < 2; i++)
		FLAC__format_entropy_coding_method_partitioned_rice_contents_init(&encoder->private_->partitioned_rice_contents_extra[i]);

	encoder->protected_->state = FLAC__STREAM_ENCODER_UNINITIALIZED;

	return encoder;
}

FLAC_API void FLAC__stream_encoder_delete(FLAC__StreamEncoder *encoder)
{
	unsigned i;

	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->private_->frame);

	encoder->private_->is_being_deleted = true;

	FLAC__stream_encoder_finish(encoder);

	if(0 != encoder->private_->verify.decoder)
		FLAC__stream_decoder_delete(encoder->private_->verify.decoder);

	for(i = 0; i < FLAC__MAX_CHANNELS; i++) {
		FLAC__format_entropy_coding_method_partitioned_rice_contents_clear(&encoder->private_->partitioned_rice_contents_workspace[i][0]);
		FLAC__format_entropy_coding_method_partitioned_rice_contents_clear(&encoder->private_->partitioned_rice_contents_workspace[i][1]);
	}
	for(i = 0; i < 2; i++) {
		FLAC__format_entropy_coding_method_partitioned_rice_contents_clear(&encoder->private_->partitioned_rice_contents_workspace_mid_side[i][0]);
		FLAC__format_entropy_coding_method_partitioned_rice_contents_clear(&encoder->private_->partitioned_rice_contents_workspace_mid_side[i][1]);
	}
	for(i = 0; i < 2; i++)
		FLAC__format_entropy_coding_method_partitioned_rice_contents_clear(&encoder->private_->partitioned_rice_contents_extra[i]);

	FLAC__bitbuffer_delete(encoder->private_->frame);
	free(encoder->private_);
	free(encoder->protected_);
	free(encoder);
}

/***********************************************************************
 *
 * Public class methods
 *
 ***********************************************************************/

FLAC_API FLAC__StreamEncoderState FLAC__stream_encoder_init(FLAC__StreamEncoder *encoder)
{
	unsigned i;
	FLAC__bool metadata_has_seektable, metadata_has_vorbis_comment;

	FLAC__ASSERT(0 != encoder);

	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return encoder->protected_->state = FLAC__STREAM_ENCODER_ALREADY_INITIALIZED;

	encoder->protected_->state = FLAC__STREAM_ENCODER_OK;

	if(0 == encoder->private_->write_callback || 0 == encoder->private_->metadata_callback)
		return encoder->protected_->state = FLAC__STREAM_ENCODER_INVALID_CALLBACK;

	if(encoder->protected_->channels == 0 || encoder->protected_->channels > FLAC__MAX_CHANNELS)
		return encoder->protected_->state = FLAC__STREAM_ENCODER_INVALID_NUMBER_OF_CHANNELS;

	if(encoder->protected_->do_mid_side_stereo && encoder->protected_->channels != 2)
		return encoder->protected_->state = FLAC__STREAM_ENCODER_MID_SIDE_CHANNELS_MISMATCH;

	if(encoder->protected_->loose_mid_side_stereo && !encoder->protected_->do_mid_side_stereo)
		return encoder->protected_->state = FLAC__STREAM_ENCODER_ILLEGAL_MID_SIDE_FORCE;

	if(encoder->protected_->bits_per_sample >= 32)
		encoder->protected_->do_mid_side_stereo = false; /* since we do 32-bit math, the side channel would have 33 bps and overflow */

	if(encoder->protected_->bits_per_sample < FLAC__MIN_BITS_PER_SAMPLE || encoder->protected_->bits_per_sample > FLAC__REFERENCE_CODEC_MAX_BITS_PER_SAMPLE)
		return encoder->protected_->state = FLAC__STREAM_ENCODER_INVALID_BITS_PER_SAMPLE;

	if(!FLAC__format_sample_rate_is_valid(encoder->protected_->sample_rate))
		return encoder->protected_->state = FLAC__STREAM_ENCODER_INVALID_SAMPLE_RATE;

	if(encoder->protected_->blocksize < FLAC__MIN_BLOCK_SIZE || encoder->protected_->blocksize > FLAC__MAX_BLOCK_SIZE)
		return encoder->protected_->state = FLAC__STREAM_ENCODER_INVALID_BLOCK_SIZE;

	if(encoder->protected_->max_lpc_order > FLAC__MAX_LPC_ORDER)
		return encoder->protected_->state = FLAC__STREAM_ENCODER_INVALID_MAX_LPC_ORDER;

	if(encoder->protected_->blocksize < encoder->protected_->max_lpc_order)
		return encoder->protected_->state = FLAC__STREAM_ENCODER_BLOCK_SIZE_TOO_SMALL_FOR_LPC_ORDER;

	if(encoder->protected_->qlp_coeff_precision == 0) {
		if(encoder->protected_->bits_per_sample < 16) {
			/* @@@ need some data about how to set this here w.r.t. blocksize and sample rate */
			/* @@@ until then we'll make a guess */
			encoder->protected_->qlp_coeff_precision = max(FLAC__MIN_QLP_COEFF_PRECISION, 2 + encoder->protected_->bits_per_sample / 2);
		}
		else if(encoder->protected_->bits_per_sample == 16) {
			if(encoder->protected_->blocksize <= 192)
				encoder->protected_->qlp_coeff_precision = 7;
			else if(encoder->protected_->blocksize <= 384)
				encoder->protected_->qlp_coeff_precision = 8;
			else if(encoder->protected_->blocksize <= 576)
				encoder->protected_->qlp_coeff_precision = 9;
			else if(encoder->protected_->blocksize <= 1152)
				encoder->protected_->qlp_coeff_precision = 10;
			else if(encoder->protected_->blocksize <= 2304)
				encoder->protected_->qlp_coeff_precision = 11;
			else if(encoder->protected_->blocksize <= 4608)
				encoder->protected_->qlp_coeff_precision = 12;
			else
				encoder->protected_->qlp_coeff_precision = 13;
		}
		else {
			if(encoder->protected_->blocksize <= 384)
				encoder->protected_->qlp_coeff_precision = FLAC__MAX_QLP_COEFF_PRECISION-2;
			else if(encoder->protected_->blocksize <= 1152)
				encoder->protected_->qlp_coeff_precision = FLAC__MAX_QLP_COEFF_PRECISION-1;
			else
				encoder->protected_->qlp_coeff_precision = FLAC__MAX_QLP_COEFF_PRECISION;
		}
		FLAC__ASSERT(encoder->protected_->qlp_coeff_precision <= FLAC__MAX_QLP_COEFF_PRECISION);
	}
	else if(encoder->protected_->qlp_coeff_precision < FLAC__MIN_QLP_COEFF_PRECISION || encoder->protected_->qlp_coeff_precision > FLAC__MAX_QLP_COEFF_PRECISION)
		return encoder->protected_->state = FLAC__STREAM_ENCODER_INVALID_QLP_COEFF_PRECISION;

	if(encoder->protected_->streamable_subset) {
		if(
			encoder->protected_->blocksize != 192 &&
			encoder->protected_->blocksize != 576 &&
			encoder->protected_->blocksize != 1152 &&
			encoder->protected_->blocksize != 2304 &&
			encoder->protected_->blocksize != 4608 &&
			encoder->protected_->blocksize != 256 &&
			encoder->protected_->blocksize != 512 &&
			encoder->protected_->blocksize != 1024 &&
			encoder->protected_->blocksize != 2048 &&
			encoder->protected_->blocksize != 4096 &&
			encoder->protected_->blocksize != 8192 &&
			encoder->protected_->blocksize != 16384
		)
			return encoder->protected_->state = FLAC__STREAM_ENCODER_NOT_STREAMABLE;
		if(
			encoder->protected_->sample_rate != 8000 &&
			encoder->protected_->sample_rate != 16000 &&
			encoder->protected_->sample_rate != 22050 &&
			encoder->protected_->sample_rate != 24000 &&
			encoder->protected_->sample_rate != 32000 &&
			encoder->protected_->sample_rate != 44100 &&
			encoder->protected_->sample_rate != 48000 &&
			encoder->protected_->sample_rate != 96000
		)
			return encoder->protected_->state = FLAC__STREAM_ENCODER_NOT_STREAMABLE;
		if(
			encoder->protected_->bits_per_sample != 8 &&
			encoder->protected_->bits_per_sample != 12 &&
			encoder->protected_->bits_per_sample != 16 &&
			encoder->protected_->bits_per_sample != 20 &&
			encoder->protected_->bits_per_sample != 24
		)
			return encoder->protected_->state = FLAC__STREAM_ENCODER_NOT_STREAMABLE;
		if(encoder->protected_->max_residual_partition_order > FLAC__SUBSET_MAX_RICE_PARTITION_ORDER)
			return encoder->protected_->state = FLAC__STREAM_ENCODER_NOT_STREAMABLE;
	}

	if(encoder->protected_->max_residual_partition_order >= (1u << FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN))
		encoder->protected_->max_residual_partition_order = (1u << FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN) - 1;
	if(encoder->protected_->min_residual_partition_order >= encoder->protected_->max_residual_partition_order)
		encoder->protected_->min_residual_partition_order = encoder->protected_->max_residual_partition_order;

	/* validate metadata */
	if(0 == encoder->protected_->metadata && encoder->protected_->num_metadata_blocks > 0)
		return encoder->protected_->state = FLAC__STREAM_ENCODER_INVALID_METADATA;
	metadata_has_seektable = false;
	metadata_has_vorbis_comment = false;
	for(i = 0; i < encoder->protected_->num_metadata_blocks; i++) {
		if(encoder->protected_->metadata[i]->type == FLAC__METADATA_TYPE_STREAMINFO)
			return encoder->protected_->state = FLAC__STREAM_ENCODER_INVALID_METADATA;
		else if(encoder->protected_->metadata[i]->type == FLAC__METADATA_TYPE_SEEKTABLE) {
			if(metadata_has_seektable) /* only one is allowed */
				return encoder->protected_->state = FLAC__STREAM_ENCODER_INVALID_METADATA;
			metadata_has_seektable = true;
			if(!FLAC__format_seektable_is_legal(&encoder->protected_->metadata[i]->data.seek_table))
				return encoder->protected_->state = FLAC__STREAM_ENCODER_INVALID_METADATA;
		}
		else if(encoder->protected_->metadata[i]->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
			if(metadata_has_vorbis_comment) /* only one is allowed */
				return encoder->protected_->state = FLAC__STREAM_ENCODER_INVALID_METADATA;
			metadata_has_vorbis_comment = true;
		}
		else if(encoder->protected_->metadata[i]->type == FLAC__METADATA_TYPE_CUESHEET) {
			if(!FLAC__format_cuesheet_is_legal(&encoder->protected_->metadata[i]->data.cue_sheet, encoder->protected_->metadata[i]->data.cue_sheet.is_cd, /*violation=*/0))
				return encoder->protected_->state = FLAC__STREAM_ENCODER_INVALID_METADATA;
		}
	}

	encoder->private_->input_capacity = 0;
	for(i = 0; i < encoder->protected_->channels; i++) {
		encoder->private_->integer_signal_unaligned[i] = encoder->private_->integer_signal[i] = 0;
		encoder->private_->real_signal_unaligned[i] = encoder->private_->real_signal[i] = 0;
	}
	for(i = 0; i < 2; i++) {
		encoder->private_->integer_signal_mid_side_unaligned[i] = encoder->private_->integer_signal_mid_side[i] = 0;
		encoder->private_->real_signal_mid_side_unaligned[i] = encoder->private_->real_signal_mid_side[i] = 0;
	}
	for(i = 0; i < encoder->protected_->channels; i++) {
		encoder->private_->residual_workspace_unaligned[i][0] = encoder->private_->residual_workspace[i][0] = 0;
		encoder->private_->residual_workspace_unaligned[i][1] = encoder->private_->residual_workspace[i][1] = 0;
		encoder->private_->best_subframe[i] = 0;
	}
	for(i = 0; i < 2; i++) {
		encoder->private_->residual_workspace_mid_side_unaligned[i][0] = encoder->private_->residual_workspace_mid_side[i][0] = 0;
		encoder->private_->residual_workspace_mid_side_unaligned[i][1] = encoder->private_->residual_workspace_mid_side[i][1] = 0;
		encoder->private_->best_subframe_mid_side[i] = 0;
	}
	encoder->private_->abs_residual_unaligned = encoder->private_->abs_residual = 0;
	encoder->private_->abs_residual_partition_sums_unaligned = encoder->private_->abs_residual_partition_sums = 0;
	encoder->private_->raw_bits_per_partition_unaligned = encoder->private_->raw_bits_per_partition = 0;
	encoder->private_->loose_mid_side_stereo_frames_exact = (double)encoder->protected_->sample_rate * 0.4 / (double)encoder->protected_->blocksize;
	encoder->private_->loose_mid_side_stereo_frames = (unsigned)(encoder->private_->loose_mid_side_stereo_frames_exact + 0.5);
	if(encoder->private_->loose_mid_side_stereo_frames == 0)
		encoder->private_->loose_mid_side_stereo_frames = 1;
	encoder->private_->loose_mid_side_stereo_frame_count = 0;
	encoder->private_->current_sample_number = 0;
	encoder->private_->current_frame_number = 0;

	encoder->private_->use_wide_by_block = (encoder->protected_->bits_per_sample + FLAC__bitmath_ilog2(encoder->protected_->blocksize)+1 > 30);
	encoder->private_->use_wide_by_order = (encoder->protected_->bits_per_sample + FLAC__bitmath_ilog2(max(encoder->protected_->max_lpc_order, FLAC__MAX_FIXED_ORDER))+1 > 30); /*@@@ need to use this? */
	encoder->private_->use_wide_by_partition = (false); /*@@@ need to set this */

	/*
	 * get the CPU info and set the function pointers
	 */
	FLAC__cpu_info(&encoder->private_->cpuinfo);
	/* first default to the non-asm routines */
	encoder->private_->local_lpc_compute_autocorrelation = FLAC__lpc_compute_autocorrelation;
	encoder->private_->local_fixed_compute_best_predictor = FLAC__fixed_compute_best_predictor;
	encoder->private_->local_lpc_compute_residual_from_qlp_coefficients = FLAC__lpc_compute_residual_from_qlp_coefficients;
	encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_64bit = FLAC__lpc_compute_residual_from_qlp_coefficients_wide;
	encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit = FLAC__lpc_compute_residual_from_qlp_coefficients;
	/* now override with asm where appropriate */
#ifndef FLAC__NO_ASM
	if(encoder->private_->cpuinfo.use_asm) {
#ifdef FLAC__CPU_IA32
		FLAC__ASSERT(encoder->private_->cpuinfo.type == FLAC__CPUINFO_TYPE_IA32);
#ifdef FLAC__HAS_NASM
#ifdef FLAC__SSE_OS
		if(encoder->private_->cpuinfo.data.ia32.sse) {
			if(encoder->protected_->max_lpc_order < 4)
				encoder->private_->local_lpc_compute_autocorrelation = FLAC__lpc_compute_autocorrelation_asm_ia32_sse_lag_4;
			else if(encoder->protected_->max_lpc_order < 8)
				encoder->private_->local_lpc_compute_autocorrelation = FLAC__lpc_compute_autocorrelation_asm_ia32_sse_lag_8;
			else if(encoder->protected_->max_lpc_order < 12)
				encoder->private_->local_lpc_compute_autocorrelation = FLAC__lpc_compute_autocorrelation_asm_ia32_sse_lag_12;
			else
				encoder->private_->local_lpc_compute_autocorrelation = FLAC__lpc_compute_autocorrelation_asm_ia32;
		}
		else
#endif
		if(encoder->private_->cpuinfo.data.ia32._3dnow)
			encoder->private_->local_lpc_compute_autocorrelation = FLAC__lpc_compute_autocorrelation_asm_ia32_3dnow;
		else
			encoder->private_->local_lpc_compute_autocorrelation = FLAC__lpc_compute_autocorrelation_asm_ia32;
		if(encoder->private_->cpuinfo.data.ia32.mmx && encoder->private_->cpuinfo.data.ia32.cmov)
			encoder->private_->local_fixed_compute_best_predictor = FLAC__fixed_compute_best_predictor_asm_ia32_mmx_cmov;
		if(encoder->private_->cpuinfo.data.ia32.mmx) {
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients = FLAC__lpc_compute_residual_from_qlp_coefficients_asm_ia32;
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit = FLAC__lpc_compute_residual_from_qlp_coefficients_asm_ia32_mmx;
		}
		else {
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients = FLAC__lpc_compute_residual_from_qlp_coefficients_asm_ia32;
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit = FLAC__lpc_compute_residual_from_qlp_coefficients_asm_ia32;
		}
#endif
#endif
	}
#endif
	/* finally override based on wide-ness if necessary */
	if(encoder->private_->use_wide_by_block) {
		encoder->private_->local_fixed_compute_best_predictor = FLAC__fixed_compute_best_predictor_wide;
	}

	/* we require precompute_partition_sums if do_escape_coding because of their intertwined nature */
	encoder->private_->precompute_partition_sums = (encoder->protected_->max_residual_partition_order > encoder->protected_->min_residual_partition_order) || encoder->protected_->do_escape_coding;

	if(!resize_buffers_(encoder, encoder->protected_->blocksize)) {
		/* the above function sets the state for us in case of an error */
		return encoder->protected_->state;
	}

	if(!FLAC__bitbuffer_init(encoder->private_->frame))
		return encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;

	/*
	 * Set up the verify stuff if necessary
	 */
	if(encoder->protected_->verify) {
		/*
		 * First, set up the fifo which will hold the
		 * original signal to compare against
		 */
		encoder->private_->verify.input_fifo.size = encoder->protected_->blocksize;
		for(i = 0; i < encoder->protected_->channels; i++) {
			if(0 == (encoder->private_->verify.input_fifo.data[i] = (FLAC__int32*)malloc(sizeof(FLAC__int32) * encoder->private_->verify.input_fifo.size)))
				return encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		}
		encoder->private_->verify.input_fifo.tail = 0;

		/*
		 * Now set up a stream decoder for verification
		 */
		encoder->private_->verify.decoder = FLAC__stream_decoder_new();
		if(0 == encoder->private_->verify.decoder)
			return encoder->protected_->state = FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR;

		FLAC__stream_decoder_set_read_callback(encoder->private_->verify.decoder, verify_read_callback_);
		FLAC__stream_decoder_set_write_callback(encoder->private_->verify.decoder, verify_write_callback_);
		FLAC__stream_decoder_set_metadata_callback(encoder->private_->verify.decoder, verify_metadata_callback_);
		FLAC__stream_decoder_set_error_callback(encoder->private_->verify.decoder, verify_error_callback_);
		FLAC__stream_decoder_set_client_data(encoder->private_->verify.decoder, encoder);
		if(FLAC__stream_decoder_init(encoder->private_->verify.decoder) != FLAC__STREAM_DECODER_SEARCH_FOR_METADATA)
			return encoder->protected_->state = FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR;
	}
	encoder->private_->verify.error_stats.absolute_sample = 0;
	encoder->private_->verify.error_stats.frame_number = 0;
	encoder->private_->verify.error_stats.channel = 0;
	encoder->private_->verify.error_stats.sample = 0;
	encoder->private_->verify.error_stats.expected = 0;
	encoder->private_->verify.error_stats.got = 0;

	/*
	 * write the stream header
	 */
	if(encoder->protected_->verify)
		encoder->private_->verify.state_hint = ENCODER_IN_MAGIC;
	if(!FLAC__bitbuffer_write_raw_uint32(encoder->private_->frame, FLAC__STREAM_SYNC, FLAC__STREAM_SYNC_LEN))
		return encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
	if(!write_bitbuffer_(encoder, 0)) {
		/* the above function sets the state for us in case of an error */
		return encoder->protected_->state;
	}

	/*
	 * write the STREAMINFO metadata block
	 */
	if(encoder->protected_->verify)
		encoder->private_->verify.state_hint = ENCODER_IN_METADATA;
	encoder->private_->metadata.type = FLAC__METADATA_TYPE_STREAMINFO;
	encoder->private_->metadata.is_last = false; /* we will have at a minimum a VORBIS_COMMENT afterwards */
	encoder->private_->metadata.length = FLAC__STREAM_METADATA_STREAMINFO_LENGTH;
	encoder->private_->metadata.data.stream_info.min_blocksize = encoder->protected_->blocksize; /* this encoder uses the same blocksize for the whole stream */
	encoder->private_->metadata.data.stream_info.max_blocksize = encoder->protected_->blocksize;
	encoder->private_->metadata.data.stream_info.min_framesize = 0; /* we don't know this yet; have to fill it in later */
	encoder->private_->metadata.data.stream_info.max_framesize = 0; /* we don't know this yet; have to fill it in later */
	encoder->private_->metadata.data.stream_info.sample_rate = encoder->protected_->sample_rate;
	encoder->private_->metadata.data.stream_info.channels = encoder->protected_->channels;
	encoder->private_->metadata.data.stream_info.bits_per_sample = encoder->protected_->bits_per_sample;
	encoder->private_->metadata.data.stream_info.total_samples = encoder->protected_->total_samples_estimate; /* we will replace this later with the real total */
	memset(encoder->private_->metadata.data.stream_info.md5sum, 0, 16); /* we don't know this yet; have to fill it in later */
	FLAC__MD5Init(&encoder->private_->md5context);
	if(!FLAC__bitbuffer_clear(encoder->private_->frame))
		return encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
	if(!FLAC__add_metadata_block(&encoder->private_->metadata, encoder->private_->frame))
		return encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
	if(!write_bitbuffer_(encoder, 0)) {
		/* the above function sets the state for us in case of an error */
		return encoder->protected_->state;
	}

	/*
	 * Now that the STREAMINFO block is written, we can init this to an
	 * absurdly-high value...
	 */
	encoder->private_->metadata.data.stream_info.min_framesize = (1u << FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN) - 1;
	/* ... and clear this to 0 */
	encoder->private_->metadata.data.stream_info.total_samples = 0;

	/*
	 * Check to see if the supplied metadata contains a VORBIS_COMMENT;
	 * if not, we will write an empty one (FLAC__add_metadata_block()
	 * automatically supplies the vendor string).
	 *
	 * WATCHOUT: libOggFLAC depends on us to write this block after the
	 * STREAMINFO since that's what the mapping requires.  (In the case
	 * that metadata_has_vorbis_comment it true it will have already
	 * insured that the metadata list is properly ordered.)
	 */
	if(!metadata_has_vorbis_comment) {
		FLAC__StreamMetadata vorbis_comment;
		vorbis_comment.type = FLAC__METADATA_TYPE_VORBIS_COMMENT;
		vorbis_comment.is_last = (encoder->protected_->num_metadata_blocks == 0);
		vorbis_comment.length = 4 + 4; /* MAGIC NUMBER */
		vorbis_comment.data.vorbis_comment.vendor_string.length = 0;
		vorbis_comment.data.vorbis_comment.vendor_string.entry = 0;
		vorbis_comment.data.vorbis_comment.num_comments = 0;
		vorbis_comment.data.vorbis_comment.comments = 0;
		if(!FLAC__bitbuffer_clear(encoder->private_->frame))
			return encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		if(!FLAC__add_metadata_block(&vorbis_comment, encoder->private_->frame))
			return encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
		if(!write_bitbuffer_(encoder, 0)) {
			/* the above function sets the state for us in case of an error */
			return encoder->protected_->state;
		}
	}

	/*
	 * write the user's metadata blocks
	 */
	for(i = 0; i < encoder->protected_->num_metadata_blocks; i++) {
		encoder->protected_->metadata[i]->is_last = (i == encoder->protected_->num_metadata_blocks - 1);
		if(!FLAC__bitbuffer_clear(encoder->private_->frame))
			return encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		if(!FLAC__add_metadata_block(encoder->protected_->metadata[i], encoder->private_->frame))
			return encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
		if(!write_bitbuffer_(encoder, 0)) {
			/* the above function sets the state for us in case of an error */
			return encoder->protected_->state;
		}
	}

	if(encoder->protected_->verify)
		encoder->private_->verify.state_hint = ENCODER_IN_AUDIO;

	return encoder->protected_->state;
}

FLAC_API void FLAC__stream_encoder_finish(FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);

	if(encoder->protected_->state == FLAC__STREAM_ENCODER_UNINITIALIZED)
		return;

	if(encoder->protected_->state == FLAC__STREAM_ENCODER_OK && !encoder->private_->is_being_deleted) {
		if(encoder->private_->current_sample_number != 0) {
			encoder->protected_->blocksize = encoder->private_->current_sample_number;
			process_frame_(encoder, true); /* true => is last frame */
		}
	}

	FLAC__MD5Final(encoder->private_->metadata.data.stream_info.md5sum, &encoder->private_->md5context);

	if(encoder->protected_->state == FLAC__STREAM_ENCODER_OK && !encoder->private_->is_being_deleted) {
		encoder->private_->metadata_callback(encoder, &encoder->private_->metadata, encoder->private_->client_data);
	}

	if(encoder->protected_->verify && 0 != encoder->private_->verify.decoder)
		FLAC__stream_decoder_finish(encoder->private_->verify.decoder);

	free_(encoder);
	set_defaults_(encoder);

	encoder->protected_->state = FLAC__STREAM_ENCODER_UNINITIALIZED;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_verify(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->verify = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_streamable_subset(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->streamable_subset = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_do_mid_side_stereo(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->do_mid_side_stereo = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_loose_mid_side_stereo(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->loose_mid_side_stereo = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_channels(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->channels = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_bits_per_sample(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->bits_per_sample = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_sample_rate(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->sample_rate = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_blocksize(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->blocksize = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_max_lpc_order(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->max_lpc_order = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_qlp_coeff_precision(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->qlp_coeff_precision = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_do_qlp_coeff_prec_search(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->do_qlp_coeff_prec_search = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_do_escape_coding(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
#if 0
	/*@@@ deprecated: */
	encoder->protected_->do_escape_coding = value;
#else
	(void)value;
#endif
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_do_exhaustive_model_search(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->do_exhaustive_model_search = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_min_residual_partition_order(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->min_residual_partition_order = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_max_residual_partition_order(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->max_residual_partition_order = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_rice_parameter_search_dist(FLAC__StreamEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
#if 0
	/*@@@ deprecated: */
	encoder->protected_->rice_parameter_search_dist = value;
#else
	(void)value;
#endif
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_total_samples_estimate(FLAC__StreamEncoder *encoder, FLAC__uint64 value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->total_samples_estimate = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_metadata(FLAC__StreamEncoder *encoder, FLAC__StreamMetadata **metadata, unsigned num_blocks)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->metadata = metadata;
	encoder->protected_->num_metadata_blocks = num_blocks;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_write_callback(FLAC__StreamEncoder *encoder, FLAC__StreamEncoderWriteCallback value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != value);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->private_->write_callback = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_metadata_callback(FLAC__StreamEncoder *encoder, FLAC__StreamEncoderMetadataCallback value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != value);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->private_->metadata_callback = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_set_client_data(FLAC__StreamEncoder *encoder, void *value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->private_->client_data = value;
	return true;
}

/*
 * These three functions are not static, but not publically exposed in
 * include/FLAC/ either.  They are used by the test suite.
 */
FLAC_API FLAC__bool FLAC__stream_encoder_disable_constant_subframes(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->private_->disable_constant_subframes = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_disable_fixed_subframes(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->private_->disable_fixed_subframes = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_disable_verbatim_subframes(FLAC__StreamEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->private_->disable_verbatim_subframes = value;
	return true;
}

FLAC_API FLAC__StreamEncoderState FLAC__stream_encoder_get_state(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->state;
}

FLAC_API FLAC__StreamDecoderState FLAC__stream_encoder_get_verify_decoder_state(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	if(encoder->protected_->verify)
		return FLAC__stream_decoder_get_state(encoder->private_->verify.decoder);
	else
		return FLAC__STREAM_DECODER_UNINITIALIZED;
}

FLAC_API const char *FLAC__stream_encoder_get_resolved_state_string(const FLAC__StreamEncoder *encoder)
{
	if(encoder->protected_->state != FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR)
		return FLAC__StreamEncoderStateString[encoder->protected_->state];
	else
		return FLAC__stream_decoder_get_resolved_state_string(encoder->private_->verify.decoder);
}

FLAC_API void FLAC__stream_encoder_get_verify_decoder_error_stats(const FLAC__StreamEncoder *encoder, FLAC__uint64 *absolute_sample, unsigned *frame_number, unsigned *channel, unsigned *sample, FLAC__int32 *expected, FLAC__int32 *got)
{
	FLAC__ASSERT(0 != encoder);
	if(0 != absolute_sample)
		*absolute_sample = encoder->private_->verify.error_stats.absolute_sample;
	if(0 != frame_number)
		*frame_number = encoder->private_->verify.error_stats.frame_number;
	if(0 != channel)
		*channel = encoder->private_->verify.error_stats.channel;
	if(0 != sample)
		*sample = encoder->private_->verify.error_stats.sample;
	if(0 != expected)
		*expected = encoder->private_->verify.error_stats.expected;
	if(0 != got)
		*got = encoder->private_->verify.error_stats.got;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_verify(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->verify;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_streamable_subset(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->streamable_subset;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_do_mid_side_stereo(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->do_mid_side_stereo;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_loose_mid_side_stereo(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->loose_mid_side_stereo;
}

FLAC_API unsigned FLAC__stream_encoder_get_channels(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->channels;
}

FLAC_API unsigned FLAC__stream_encoder_get_bits_per_sample(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->bits_per_sample;
}

FLAC_API unsigned FLAC__stream_encoder_get_sample_rate(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->sample_rate;
}

FLAC_API unsigned FLAC__stream_encoder_get_blocksize(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->blocksize;
}

FLAC_API unsigned FLAC__stream_encoder_get_max_lpc_order(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->max_lpc_order;
}

FLAC_API unsigned FLAC__stream_encoder_get_qlp_coeff_precision(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->qlp_coeff_precision;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_do_qlp_coeff_prec_search(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->do_qlp_coeff_prec_search;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_do_escape_coding(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->do_escape_coding;
}

FLAC_API FLAC__bool FLAC__stream_encoder_get_do_exhaustive_model_search(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->do_exhaustive_model_search;
}

FLAC_API unsigned FLAC__stream_encoder_get_min_residual_partition_order(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->min_residual_partition_order;
}

FLAC_API unsigned FLAC__stream_encoder_get_max_residual_partition_order(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->max_residual_partition_order;
}

FLAC_API unsigned FLAC__stream_encoder_get_rice_parameter_search_dist(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->rice_parameter_search_dist;
}

FLAC_API FLAC__uint64 FLAC__stream_encoder_get_total_samples_estimate(const FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	return encoder->protected_->total_samples_estimate;
}

FLAC_API FLAC__bool FLAC__stream_encoder_process(FLAC__StreamEncoder *encoder, const FLAC__int32 * const buffer[], unsigned samples)
{
	unsigned i, j, channel;
	FLAC__int32 x, mid, side;
	const unsigned channels = encoder->protected_->channels, blocksize = encoder->protected_->blocksize;

	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(encoder->protected_->state == FLAC__STREAM_ENCODER_OK);

	j = 0;
	if(encoder->protected_->do_mid_side_stereo && channels == 2) {
		do {
			if(encoder->protected_->verify)
				append_to_verify_fifo_(&encoder->private_->verify.input_fifo, buffer, j, channels, min(blocksize-encoder->private_->current_sample_number, samples-j));

			for(i = encoder->private_->current_sample_number; i < blocksize && j < samples; i++, j++) {
				x = mid = side = buffer[0][j];
				encoder->private_->integer_signal[0][i] = x;
				encoder->private_->real_signal[0][i] = (FLAC__real)x;
				x = buffer[1][j];
				encoder->private_->integer_signal[1][i] = x;
				encoder->private_->real_signal[1][i] = (FLAC__real)x;
				mid += x;
				side -= x;
				mid >>= 1; /* NOTE: not the same as 'mid = (buffer[0][j] + buffer[1][j]) / 2' ! */
				encoder->private_->integer_signal_mid_side[1][i] = side;
				encoder->private_->integer_signal_mid_side[0][i] = mid;
				encoder->private_->real_signal_mid_side[1][i] = (FLAC__real)side;
				encoder->private_->real_signal_mid_side[0][i] = (FLAC__real)mid;
				encoder->private_->current_sample_number++;
			}
			if(i == blocksize) {
				if(!process_frame_(encoder, false)) /* false => not last frame */
					return false;
			}
		} while(j < samples);
	}
	else {
		do {
			if(encoder->protected_->verify)
				append_to_verify_fifo_(&encoder->private_->verify.input_fifo, buffer, j, channels, min(blocksize-encoder->private_->current_sample_number, samples-j));

			for(i = encoder->private_->current_sample_number; i < blocksize && j < samples; i++, j++) {
				for(channel = 0; channel < channels; channel++) {
					x = buffer[channel][j];
					encoder->private_->integer_signal[channel][i] = x;
					encoder->private_->real_signal[channel][i] = (FLAC__real)x;
				}
				encoder->private_->current_sample_number++;
			}
			if(i == blocksize) {
				if(!process_frame_(encoder, false)) /* false => not last frame */
					return false;
			}
		} while(j < samples);
	}

	return true;
}

FLAC_API FLAC__bool FLAC__stream_encoder_process_interleaved(FLAC__StreamEncoder *encoder, const FLAC__int32 buffer[], unsigned samples)
{
	unsigned i, j, k, channel;
	FLAC__int32 x, mid, side;
	const unsigned channels = encoder->protected_->channels, blocksize = encoder->protected_->blocksize;

	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(encoder->protected_->state == FLAC__STREAM_ENCODER_OK);

	j = k = 0;
	if(encoder->protected_->do_mid_side_stereo && channels == 2) {
		do {
			if(encoder->protected_->verify)
				append_to_verify_fifo_interleaved_(&encoder->private_->verify.input_fifo, buffer, j, channels, min(blocksize-encoder->private_->current_sample_number, samples-j));

			for(i = encoder->private_->current_sample_number; i < blocksize && j < samples; i++, j++) {
				x = mid = side = buffer[k++];
				encoder->private_->integer_signal[0][i] = x;
				encoder->private_->real_signal[0][i] = (FLAC__real)x;
				x = buffer[k++];
				encoder->private_->integer_signal[1][i] = x;
				encoder->private_->real_signal[1][i] = (FLAC__real)x;
				mid += x;
				side -= x;
				mid >>= 1; /* NOTE: not the same as 'mid = (left + right) / 2' ! */
				encoder->private_->integer_signal_mid_side[1][i] = side;
				encoder->private_->integer_signal_mid_side[0][i] = mid;
				encoder->private_->real_signal_mid_side[1][i] = (FLAC__real)side;
				encoder->private_->real_signal_mid_side[0][i] = (FLAC__real)mid;
				encoder->private_->current_sample_number++;
			}
			if(i == blocksize) {
				if(!process_frame_(encoder, false)) /* false => not last frame */
					return false;
			}
		} while(j < samples);
	}
	else {
		do {
			if(encoder->protected_->verify)
				append_to_verify_fifo_interleaved_(&encoder->private_->verify.input_fifo, buffer, j, channels, min(blocksize-encoder->private_->current_sample_number, samples-j));

			for(i = encoder->private_->current_sample_number; i < blocksize && j < samples; i++, j++) {
				for(channel = 0; channel < channels; channel++) {
					x = buffer[k++];
					encoder->private_->integer_signal[channel][i] = x;
					encoder->private_->real_signal[channel][i] = (FLAC__real)x;
				}
				encoder->private_->current_sample_number++;
			}
			if(i == blocksize) {
				if(!process_frame_(encoder, false)) /* false => not last frame */
					return false;
			}
		} while(j < samples);
	}

	return true;
}

/***********************************************************************
 *
 * Private class methods
 *
 ***********************************************************************/

void set_defaults_(FLAC__StreamEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);

	encoder->protected_->verify = false;
	encoder->protected_->streamable_subset = true;
	encoder->protected_->do_mid_side_stereo = false;
	encoder->protected_->loose_mid_side_stereo = false;
	encoder->protected_->channels = 2;
	encoder->protected_->bits_per_sample = 16;
	encoder->protected_->sample_rate = 44100;
	encoder->protected_->blocksize = 1152;
	encoder->protected_->max_lpc_order = 0;
	encoder->protected_->qlp_coeff_precision = 0;
	encoder->protected_->do_qlp_coeff_prec_search = false;
	encoder->protected_->do_exhaustive_model_search = false;
	encoder->protected_->do_escape_coding = false;
	encoder->protected_->min_residual_partition_order = 0;
	encoder->protected_->max_residual_partition_order = 0;
	encoder->protected_->rice_parameter_search_dist = 0;
	encoder->protected_->total_samples_estimate = 0;
	encoder->protected_->metadata = 0;
	encoder->protected_->num_metadata_blocks = 0;

	encoder->private_->disable_constant_subframes = false;
	encoder->private_->disable_fixed_subframes = false;
	encoder->private_->disable_verbatim_subframes = false;
	encoder->private_->write_callback = 0;
	encoder->private_->metadata_callback = 0;
	encoder->private_->client_data = 0;
}

void free_(FLAC__StreamEncoder *encoder)
{
	unsigned i, channel;

	FLAC__ASSERT(0 != encoder);
	for(i = 0; i < encoder->protected_->channels; i++) {
		if(0 != encoder->private_->integer_signal_unaligned[i]) {
			free(encoder->private_->integer_signal_unaligned[i]);
			encoder->private_->integer_signal_unaligned[i] = 0;
		}
		if(0 != encoder->private_->real_signal_unaligned[i]) {
			free(encoder->private_->real_signal_unaligned[i]);
			encoder->private_->real_signal_unaligned[i] = 0;
		}
	}
	for(i = 0; i < 2; i++) {
		if(0 != encoder->private_->integer_signal_mid_side_unaligned[i]) {
			free(encoder->private_->integer_signal_mid_side_unaligned[i]);
			encoder->private_->integer_signal_mid_side_unaligned[i] = 0;
		}
		if(0 != encoder->private_->real_signal_mid_side_unaligned[i]) {
			free(encoder->private_->real_signal_mid_side_unaligned[i]);
			encoder->private_->real_signal_mid_side_unaligned[i] = 0;
		}
	}
	for(channel = 0; channel < encoder->protected_->channels; channel++) {
		for(i = 0; i < 2; i++) {
			if(0 != encoder->private_->residual_workspace_unaligned[channel][i]) {
				free(encoder->private_->residual_workspace_unaligned[channel][i]);
				encoder->private_->residual_workspace_unaligned[channel][i] = 0;
			}
		}
	}
	for(channel = 0; channel < 2; channel++) {
		for(i = 0; i < 2; i++) {
			if(0 != encoder->private_->residual_workspace_mid_side_unaligned[channel][i]) {
				free(encoder->private_->residual_workspace_mid_side_unaligned[channel][i]);
				encoder->private_->residual_workspace_mid_side_unaligned[channel][i] = 0;
			}
		}
	}
	if(0 != encoder->private_->abs_residual_unaligned) {
		free(encoder->private_->abs_residual_unaligned);
		encoder->private_->abs_residual_unaligned = 0;
	}
	if(0 != encoder->private_->abs_residual_partition_sums_unaligned) {
		free(encoder->private_->abs_residual_partition_sums_unaligned);
		encoder->private_->abs_residual_partition_sums_unaligned = 0;
	}
	if(0 != encoder->private_->raw_bits_per_partition_unaligned) {
		free(encoder->private_->raw_bits_per_partition_unaligned);
		encoder->private_->raw_bits_per_partition_unaligned = 0;
	}
	if(encoder->protected_->verify) {
		for(i = 0; i < encoder->protected_->channels; i++) {
			if(0 != encoder->private_->verify.input_fifo.data[i]) {
				free(encoder->private_->verify.input_fifo.data[i]);
				encoder->private_->verify.input_fifo.data[i] = 0;
			}
		}
	}
	FLAC__bitbuffer_free(encoder->private_->frame);
}

FLAC__bool resize_buffers_(FLAC__StreamEncoder *encoder, unsigned new_size)
{
	FLAC__bool ok;
	unsigned i, channel;

	FLAC__ASSERT(new_size > 0);
	FLAC__ASSERT(encoder->protected_->state == FLAC__STREAM_ENCODER_OK);
	FLAC__ASSERT(encoder->private_->current_sample_number == 0);

	/* To avoid excessive malloc'ing, we only grow the buffer; no shrinking. */
	if(new_size <= encoder->private_->input_capacity)
		return true;

	ok = true;

	/* WATCHOUT: FLAC__lpc_compute_residual_from_qlp_coefficients_asm_ia32_mmx()
	 * requires that the input arrays (in our case the integer signals)
	 * have a buffer of up to 3 zeroes in front (at negative indices) for
	 * alignment purposes; we use 4 to keep the data well-aligned.
	 */

	for(i = 0; ok && i < encoder->protected_->channels; i++) {
		ok = ok && FLAC__memory_alloc_aligned_int32_array(new_size+4, &encoder->private_->integer_signal_unaligned[i], &encoder->private_->integer_signal[i]);
		ok = ok && FLAC__memory_alloc_aligned_real_array(new_size, &encoder->private_->real_signal_unaligned[i], &encoder->private_->real_signal[i]);
		memset(encoder->private_->integer_signal[i], 0, sizeof(FLAC__int32)*4);
		encoder->private_->integer_signal[i] += 4;
	}
	for(i = 0; ok && i < 2; i++) {
		ok = ok && FLAC__memory_alloc_aligned_int32_array(new_size+4, &encoder->private_->integer_signal_mid_side_unaligned[i], &encoder->private_->integer_signal_mid_side[i]);
		ok = ok && FLAC__memory_alloc_aligned_real_array(new_size, &encoder->private_->real_signal_mid_side_unaligned[i], &encoder->private_->real_signal_mid_side[i]);
		memset(encoder->private_->integer_signal_mid_side[i], 0, sizeof(FLAC__int32)*4);
		encoder->private_->integer_signal_mid_side[i] += 4;
	}
	for(channel = 0; ok && channel < encoder->protected_->channels; channel++) {
		for(i = 0; ok && i < 2; i++) {
			ok = ok && FLAC__memory_alloc_aligned_int32_array(new_size, &encoder->private_->residual_workspace_unaligned[channel][i], &encoder->private_->residual_workspace[channel][i]);
		}
	}
	for(channel = 0; ok && channel < 2; channel++) {
		for(i = 0; ok && i < 2; i++) {
			ok = ok && FLAC__memory_alloc_aligned_int32_array(new_size, &encoder->private_->residual_workspace_mid_side_unaligned[channel][i], &encoder->private_->residual_workspace_mid_side[channel][i]);
		}
	}
	ok = ok && FLAC__memory_alloc_aligned_uint32_array(new_size, &encoder->private_->abs_residual_unaligned, &encoder->private_->abs_residual);
	if(encoder->private_->precompute_partition_sums || encoder->protected_->do_escape_coding) /* we require precompute_partition_sums if do_escape_coding because of their intertwined nature */
		ok = ok && FLAC__memory_alloc_aligned_uint64_array(new_size * 2, &encoder->private_->abs_residual_partition_sums_unaligned, &encoder->private_->abs_residual_partition_sums);
	if(encoder->protected_->do_escape_coding)
		ok = ok && FLAC__memory_alloc_aligned_unsigned_array(new_size * 2, &encoder->private_->raw_bits_per_partition_unaligned, &encoder->private_->raw_bits_per_partition);

	if(ok)
		encoder->private_->input_capacity = new_size;
	else
		encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;

	return ok;
}

FLAC__bool write_bitbuffer_(FLAC__StreamEncoder *encoder, unsigned samples)
{
	const FLAC__byte *buffer;
	unsigned bytes;

	FLAC__ASSERT(FLAC__bitbuffer_is_byte_aligned(encoder->private_->frame));

	FLAC__bitbuffer_get_buffer(encoder->private_->frame, &buffer, &bytes);

	if(encoder->protected_->verify) {
		encoder->private_->verify.output.data = buffer;
		encoder->private_->verify.output.bytes = bytes;
		if(encoder->private_->verify.state_hint == ENCODER_IN_MAGIC) {
			encoder->private_->verify.needs_magic_hack = true;
		}
		else {
			if(!FLAC__stream_decoder_process_single(encoder->private_->verify.decoder)) {
				FLAC__bitbuffer_release_buffer(encoder->private_->frame);
				if(encoder->protected_->state != FLAC__STREAM_ENCODER_VERIFY_MISMATCH_IN_AUDIO_DATA)
					encoder->protected_->state = FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR;
				return false;
			}
		}
	}

	if(encoder->private_->write_callback(encoder, buffer, bytes, samples, encoder->private_->current_frame_number, encoder->private_->client_data) != FLAC__STREAM_ENCODER_WRITE_STATUS_OK) {
		FLAC__bitbuffer_release_buffer(encoder->private_->frame);
		encoder->protected_->state = FLAC__STREAM_ENCODER_FATAL_ERROR_WHILE_WRITING;
		return false;
	}

	FLAC__bitbuffer_release_buffer(encoder->private_->frame);

	if(samples > 0) {
		encoder->private_->metadata.data.stream_info.min_framesize = min(bytes, encoder->private_->metadata.data.stream_info.min_framesize);
		encoder->private_->metadata.data.stream_info.max_framesize = max(bytes, encoder->private_->metadata.data.stream_info.max_framesize);
	}

	return true;
}

FLAC__bool process_frame_(FLAC__StreamEncoder *encoder, FLAC__bool is_last_frame)
{
	FLAC__ASSERT(encoder->protected_->state == FLAC__STREAM_ENCODER_OK);

	/*
	 * Accumulate raw signal to the MD5 signature
	 */
	if(!FLAC__MD5Accumulate(&encoder->private_->md5context, (const FLAC__int32 * const *)encoder->private_->integer_signal, encoder->protected_->channels, encoder->protected_->blocksize, (encoder->protected_->bits_per_sample+7) / 8)) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	/*
	 * Process the frame header and subframes into the frame bitbuffer
	 */
	if(!process_subframes_(encoder, is_last_frame)) {
		/* the above function sets the state for us in case of an error */
		return false;
	}

	/*
	 * Zero-pad the frame to a byte_boundary
	 */
	if(!FLAC__bitbuffer_zero_pad_to_byte_boundary(encoder->private_->frame)) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	/*
	 * CRC-16 the whole thing
	 */
	FLAC__ASSERT(FLAC__bitbuffer_is_byte_aligned(encoder->private_->frame));
	FLAC__bitbuffer_write_raw_uint32(encoder->private_->frame, FLAC__bitbuffer_get_write_crc16(encoder->private_->frame), FLAC__FRAME_FOOTER_CRC_LEN);

	/*
	 * Write it
	 */
	if(!write_bitbuffer_(encoder, encoder->protected_->blocksize)) {
		/* the above function sets the state for us in case of an error */
		return false;
	}

	/*
	 * Get ready for the next frame
	 */
	encoder->private_->current_sample_number = 0;
	encoder->private_->current_frame_number++;
	encoder->private_->metadata.data.stream_info.total_samples += (FLAC__uint64)encoder->protected_->blocksize;

	return true;
}

FLAC__bool process_subframes_(FLAC__StreamEncoder *encoder, FLAC__bool is_last_frame)
{
	FLAC__FrameHeader frame_header;
	unsigned channel, min_partition_order = encoder->protected_->min_residual_partition_order, max_partition_order;
	FLAC__bool do_independent, do_mid_side, precompute_partition_sums;

	/*
	 * Calculate the min,max Rice partition orders
	 */
	if(is_last_frame) {
		max_partition_order = 0;
	}
	else {
		max_partition_order = FLAC__format_get_max_rice_partition_order_from_blocksize(encoder->protected_->blocksize);
		max_partition_order = min(max_partition_order, encoder->protected_->max_residual_partition_order);
	}
	min_partition_order = min(min_partition_order, max_partition_order);

	precompute_partition_sums = encoder->private_->precompute_partition_sums && ((max_partition_order > min_partition_order) || encoder->protected_->do_escape_coding);

	/*
	 * Setup the frame
	 */
	if(!FLAC__bitbuffer_clear(encoder->private_->frame)) {
		encoder->protected_->state = FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	frame_header.blocksize = encoder->protected_->blocksize;
	frame_header.sample_rate = encoder->protected_->sample_rate;
	frame_header.channels = encoder->protected_->channels;
	frame_header.channel_assignment = FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT; /* the default unless the encoder determines otherwise */
	frame_header.bits_per_sample = encoder->protected_->bits_per_sample;
	frame_header.number_type = FLAC__FRAME_NUMBER_TYPE_FRAME_NUMBER;
	frame_header.number.frame_number = encoder->private_->current_frame_number;

	/*
	 * Figure out what channel assignments to try
	 */
	if(encoder->protected_->do_mid_side_stereo) {
		if(encoder->protected_->loose_mid_side_stereo) {
			if(encoder->private_->loose_mid_side_stereo_frame_count == 0) {
				do_independent = true;
				do_mid_side = true;
			}
			else {
				do_independent = (encoder->private_->last_channel_assignment == FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT);
				do_mid_side = !do_independent;
			}
		}
		else {
			do_independent = true;
			do_mid_side = true;
		}
	}
	else {
		do_independent = true;
		do_mid_side = false;
	}

	FLAC__ASSERT(do_independent || do_mid_side);

	/*
	 * Check for wasted bits; set effective bps for each subframe
	 */
	if(do_independent) {
		for(channel = 0; channel < encoder->protected_->channels; channel++) {
			const unsigned w = get_wasted_bits_(encoder->private_->integer_signal[channel], encoder->protected_->blocksize);
			encoder->private_->subframe_workspace[channel][0].wasted_bits = encoder->private_->subframe_workspace[channel][1].wasted_bits = w;
			encoder->private_->subframe_bps[channel] = encoder->protected_->bits_per_sample - w;
		}
	}
	if(do_mid_side) {
		FLAC__ASSERT(encoder->protected_->channels == 2);
		for(channel = 0; channel < 2; channel++) {
			const unsigned w = get_wasted_bits_(encoder->private_->integer_signal_mid_side[channel], encoder->protected_->blocksize);
			encoder->private_->subframe_workspace_mid_side[channel][0].wasted_bits = encoder->private_->subframe_workspace_mid_side[channel][1].wasted_bits = w;
			encoder->private_->subframe_bps_mid_side[channel] = encoder->protected_->bits_per_sample - w + (channel==0? 0:1);
		}
	}

	/*
	 * First do a normal encoding pass of each independent channel
	 */
	if(do_independent) {
		for(channel = 0; channel < encoder->protected_->channels; channel++) {
			if(!
				process_subframe_(
					encoder,
					min_partition_order,
					max_partition_order,
					precompute_partition_sums,
					&frame_header,
					encoder->private_->subframe_bps[channel],
					encoder->private_->integer_signal[channel],
					encoder->private_->real_signal[channel],
					encoder->private_->subframe_workspace_ptr[channel],
					encoder->private_->partitioned_rice_contents_workspace_ptr[channel],
					encoder->private_->residual_workspace[channel],
					encoder->private_->best_subframe+channel,
					encoder->private_->best_subframe_bits+channel
				)
			)
				return false;
		}
	}

	/*
	 * Now do mid and side channels if requested
	 */
	if(do_mid_side) {
		FLAC__ASSERT(encoder->protected_->channels == 2);

		for(channel = 0; channel < 2; channel++) {
			if(!
				process_subframe_(
					encoder,
					min_partition_order,
					max_partition_order,
					precompute_partition_sums,
					&frame_header,
					encoder->private_->subframe_bps_mid_side[channel],
					encoder->private_->integer_signal_mid_side[channel],
					encoder->private_->real_signal_mid_side[channel],
					encoder->private_->subframe_workspace_ptr_mid_side[channel],
					encoder->private_->partitioned_rice_contents_workspace_ptr_mid_side[channel],
					encoder->private_->residual_workspace_mid_side[channel],
					encoder->private_->best_subframe_mid_side+channel,
					encoder->private_->best_subframe_bits_mid_side+channel
				)
			)
				return false;
		}
	}

	/*
	 * Compose the frame bitbuffer
	 */
	if(do_mid_side) {
		unsigned left_bps = 0, right_bps = 0; /* initialized only to prevent superfluous compiler warning */
		FLAC__Subframe *left_subframe = 0, *right_subframe = 0; /* initialized only to prevent superfluous compiler warning */
		FLAC__ChannelAssignment channel_assignment;

		FLAC__ASSERT(encoder->protected_->channels == 2);

		if(encoder->protected_->loose_mid_side_stereo && encoder->private_->loose_mid_side_stereo_frame_count > 0) {
			channel_assignment = (encoder->private_->last_channel_assignment == FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT? FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT : FLAC__CHANNEL_ASSIGNMENT_MID_SIDE);
		}
		else {
			unsigned bits[4]; /* WATCHOUT - indexed by FLAC__ChannelAssignment */
			unsigned min_bits;
			FLAC__ChannelAssignment ca;

			FLAC__ASSERT(do_independent && do_mid_side);

			/* We have to figure out which channel assignent results in the smallest frame */
			bits[FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT] = encoder->private_->best_subframe_bits         [0] + encoder->private_->best_subframe_bits         [1];
			bits[FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE  ] = encoder->private_->best_subframe_bits         [0] + encoder->private_->best_subframe_bits_mid_side[1];
			bits[FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE ] = encoder->private_->best_subframe_bits         [1] + encoder->private_->best_subframe_bits_mid_side[1];
			bits[FLAC__CHANNEL_ASSIGNMENT_MID_SIDE   ] = encoder->private_->best_subframe_bits_mid_side[0] + encoder->private_->best_subframe_bits_mid_side[1];

			for(channel_assignment = (FLAC__ChannelAssignment)0, min_bits = bits[0], ca = (FLAC__ChannelAssignment)1; (int)ca <= 3; ca = (FLAC__ChannelAssignment)((int)ca + 1)) {
				if(bits[ca] < min_bits) {
					min_bits = bits[ca];
					channel_assignment = ca;
				}
			}
		}

		frame_header.channel_assignment = channel_assignment;

		if(!FLAC__frame_add_header(&frame_header, encoder->protected_->streamable_subset, encoder->private_->frame)) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
			return false;
		}

		switch(channel_assignment) {
			case FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT:
				left_subframe  = &encoder->private_->subframe_workspace         [0][encoder->private_->best_subframe         [0]];
				right_subframe = &encoder->private_->subframe_workspace         [1][encoder->private_->best_subframe         [1]];
				break;
			case FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE:
				left_subframe  = &encoder->private_->subframe_workspace         [0][encoder->private_->best_subframe         [0]];
				right_subframe = &encoder->private_->subframe_workspace_mid_side[1][encoder->private_->best_subframe_mid_side[1]];
				break;
			case FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE:
				left_subframe  = &encoder->private_->subframe_workspace_mid_side[1][encoder->private_->best_subframe_mid_side[1]];
				right_subframe = &encoder->private_->subframe_workspace         [1][encoder->private_->best_subframe         [1]];
				break;
			case FLAC__CHANNEL_ASSIGNMENT_MID_SIDE:
				left_subframe  = &encoder->private_->subframe_workspace_mid_side[0][encoder->private_->best_subframe_mid_side[0]];
				right_subframe = &encoder->private_->subframe_workspace_mid_side[1][encoder->private_->best_subframe_mid_side[1]];
				break;
			default:
				FLAC__ASSERT(0);
		}

		switch(channel_assignment) {
			case FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT:
				left_bps  = encoder->private_->subframe_bps         [0];
				right_bps = encoder->private_->subframe_bps         [1];
				break;
			case FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE:
				left_bps  = encoder->private_->subframe_bps         [0];
				right_bps = encoder->private_->subframe_bps_mid_side[1];
				break;
			case FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE:
				left_bps  = encoder->private_->subframe_bps_mid_side[1];
				right_bps = encoder->private_->subframe_bps         [1];
				break;
			case FLAC__CHANNEL_ASSIGNMENT_MID_SIDE:
				left_bps  = encoder->private_->subframe_bps_mid_side[0];
				right_bps = encoder->private_->subframe_bps_mid_side[1];
				break;
			default:
				FLAC__ASSERT(0);
		}

		/* note that encoder_add_subframe_ sets the state for us in case of an error */
		if(!add_subframe_(encoder, &frame_header, left_bps , left_subframe , encoder->private_->frame))
			return false;
		if(!add_subframe_(encoder, &frame_header, right_bps, right_subframe, encoder->private_->frame))
			return false;
	}
	else {
		if(!FLAC__frame_add_header(&frame_header, encoder->protected_->streamable_subset, encoder->private_->frame)) {
			encoder->protected_->state = FLAC__STREAM_ENCODER_FRAMING_ERROR;
			return false;
		}

		for(channel = 0; channel < encoder->protected_->channels; channel++) {
			if(!add_subframe_(encoder, &frame_header, encoder->private_->subframe_bps[channel], &encoder->private_->subframe_workspace[channel][encoder->private_->best_subframe[channel]], encoder->private_->frame)) {
				/* the above function sets the state for us in case of an error */
				return false;
			}
		}
	}

	if(encoder->protected_->loose_mid_side_stereo) {
		encoder->private_->loose_mid_side_stereo_frame_count++;
		if(encoder->private_->loose_mid_side_stereo_frame_count >= encoder->private_->loose_mid_side_stereo_frames)
			encoder->private_->loose_mid_side_stereo_frame_count = 0;
	}

	encoder->private_->last_channel_assignment = frame_header.channel_assignment;

	return true;
}

FLAC__bool process_subframe_(
	FLAC__StreamEncoder *encoder,
	unsigned min_partition_order,
	unsigned max_partition_order,
	FLAC__bool precompute_partition_sums,
	const FLAC__FrameHeader *frame_header,
	unsigned subframe_bps,
	const FLAC__int32 integer_signal[],
	const FLAC__real real_signal[],
	FLAC__Subframe *subframe[2],
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents[2],
	FLAC__int32 *residual[2],
	unsigned *best_subframe,
	unsigned *best_bits
)
{
	FLAC__real fixed_residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1];
	FLAC__real lpc_residual_bits_per_sample;
	FLAC__real autoc[FLAC__MAX_LPC_ORDER+1]; /* WATCHOUT: the size is important even though encoder->protected_->max_lpc_order might be less; some asm routines need all the space */
	FLAC__real lpc_error[FLAC__MAX_LPC_ORDER];
	unsigned min_lpc_order, max_lpc_order, lpc_order;
	unsigned min_fixed_order, max_fixed_order, guess_fixed_order, fixed_order;
	unsigned min_qlp_coeff_precision, max_qlp_coeff_precision, qlp_coeff_precision;
	unsigned rice_parameter;
	unsigned _candidate_bits, _best_bits;
	unsigned _best_subframe;

	/* verbatim subframe is the baseline against which we measure other compressed subframes */
	_best_subframe = 0;
	if(encoder->private_->disable_verbatim_subframes && frame_header->blocksize >= FLAC__MAX_FIXED_ORDER)
		_best_bits = UINT_MAX;
	else
		_best_bits = evaluate_verbatim_subframe_(integer_signal, frame_header->blocksize, subframe_bps, subframe[_best_subframe]);

	if(frame_header->blocksize >= FLAC__MAX_FIXED_ORDER) {
		unsigned signal_is_constant = false;
		guess_fixed_order = encoder->private_->local_fixed_compute_best_predictor(integer_signal+FLAC__MAX_FIXED_ORDER, frame_header->blocksize-FLAC__MAX_FIXED_ORDER, fixed_residual_bits_per_sample);
		/* check for constant subframe */
		if(!encoder->private_->disable_constant_subframes && fixed_residual_bits_per_sample[1] == 0.0) {
			/* the above means integer_signal+FLAC__MAX_FIXED_ORDER is constant, now we just have to check the warmup samples */
			unsigned i;
			signal_is_constant = true;
			for(i = 1; i <= FLAC__MAX_FIXED_ORDER; i++) {
				if(integer_signal[0] != integer_signal[i]) {
					signal_is_constant = false;
					break;
				}
			}
		}
		if(signal_is_constant) {
			_candidate_bits = evaluate_constant_subframe_(integer_signal[0], subframe_bps, subframe[!_best_subframe]);
			if(_candidate_bits < _best_bits) {
				_best_subframe = !_best_subframe;
				_best_bits = _candidate_bits;
			}
		}
		else {
			if(!encoder->private_->disable_fixed_subframes || (encoder->protected_->max_lpc_order == 0 && _best_bits == UINT_MAX)) {
				/* encode fixed */
				if(encoder->protected_->do_exhaustive_model_search) {
					min_fixed_order = 0;
					max_fixed_order = FLAC__MAX_FIXED_ORDER;
				}
				else {
					min_fixed_order = max_fixed_order = guess_fixed_order;
				}
				for(fixed_order = min_fixed_order; fixed_order <= max_fixed_order; fixed_order++) {
					if(fixed_residual_bits_per_sample[fixed_order] >= (FLAC__real)subframe_bps)
						continue; /* don't even try */
					rice_parameter = (fixed_residual_bits_per_sample[fixed_order] > 0.0)? (unsigned)(fixed_residual_bits_per_sample[fixed_order]+0.5) : 0; /* 0.5 is for rounding */
#ifndef FLAC__SYMMETRIC_RICE
					rice_parameter++; /* to account for the signed->unsigned conversion during rice coding */
#endif
					if(rice_parameter >= FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER) {
#ifdef DEBUG_VERBOSE
						fprintf(stderr, "clipping rice_parameter (%u -> %u) @0\n", rice_parameter, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1);
#endif
						rice_parameter = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1;
					}
					_candidate_bits =
						evaluate_fixed_subframe_(
							encoder,
							integer_signal,
							residual[!_best_subframe],
							encoder->private_->abs_residual,
							encoder->private_->abs_residual_partition_sums,
							encoder->private_->raw_bits_per_partition,
							frame_header->blocksize,
							subframe_bps,
							fixed_order,
							rice_parameter,
							min_partition_order,
							max_partition_order,
							precompute_partition_sums,
							encoder->protected_->do_escape_coding,
							encoder->protected_->rice_parameter_search_dist,
							subframe[!_best_subframe],
							partitioned_rice_contents[!_best_subframe]
						);
					if(_candidate_bits < _best_bits) {
						_best_subframe = !_best_subframe;
						_best_bits = _candidate_bits;
					}
				}
			}

			/* encode lpc */
			if(encoder->protected_->max_lpc_order > 0) {
				if(encoder->protected_->max_lpc_order >= frame_header->blocksize)
					max_lpc_order = frame_header->blocksize-1;
				else
					max_lpc_order = encoder->protected_->max_lpc_order;
				if(max_lpc_order > 0) {
					encoder->private_->local_lpc_compute_autocorrelation(real_signal, frame_header->blocksize, max_lpc_order+1, autoc);
					/* if autoc[0] == 0.0, the signal is constant and we usually won't get here, but it can happen */
					if(autoc[0] != 0.0) {
						FLAC__lpc_compute_lp_coefficients(autoc, max_lpc_order, encoder->private_->lp_coeff, lpc_error);
						if(encoder->protected_->do_exhaustive_model_search) {
							min_lpc_order = 1;
						}
						else {
							unsigned guess_lpc_order = FLAC__lpc_compute_best_order(lpc_error, max_lpc_order, frame_header->blocksize, subframe_bps);
							min_lpc_order = max_lpc_order = guess_lpc_order;
						}
						for(lpc_order = min_lpc_order; lpc_order <= max_lpc_order; lpc_order++) {
							lpc_residual_bits_per_sample = FLAC__lpc_compute_expected_bits_per_residual_sample(lpc_error[lpc_order-1], frame_header->blocksize-lpc_order);
							if(lpc_residual_bits_per_sample >= (FLAC__real)subframe_bps)
								continue; /* don't even try */
							rice_parameter = (lpc_residual_bits_per_sample > 0.0)? (unsigned)(lpc_residual_bits_per_sample+0.5) : 0; /* 0.5 is for rounding */
#ifndef FLAC__SYMMETRIC_RICE
							rice_parameter++; /* to account for the signed->unsigned conversion during rice coding */
#endif
							if(rice_parameter >= FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER) {
#ifdef DEBUG_VERBOSE
								fprintf(stderr, "clipping rice_parameter (%u -> %u) @1\n", rice_parameter, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1);
#endif
								rice_parameter = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1;
							}
							if(encoder->protected_->do_qlp_coeff_prec_search) {
								min_qlp_coeff_precision = FLAC__MIN_QLP_COEFF_PRECISION;
								/* ensure a 32-bit datapath throughout for 16bps or less */
								if(subframe_bps <= 16)
									max_qlp_coeff_precision = min(32 - subframe_bps - lpc_order, FLAC__MAX_QLP_COEFF_PRECISION);
								else
									max_qlp_coeff_precision = FLAC__MAX_QLP_COEFF_PRECISION;
							}
							else {
								min_qlp_coeff_precision = max_qlp_coeff_precision = encoder->protected_->qlp_coeff_precision;
							}
							for(qlp_coeff_precision = min_qlp_coeff_precision; qlp_coeff_precision <= max_qlp_coeff_precision; qlp_coeff_precision++) {
								_candidate_bits =
									evaluate_lpc_subframe_(
										encoder,
										integer_signal,
										residual[!_best_subframe],
										encoder->private_->abs_residual,
										encoder->private_->abs_residual_partition_sums,
										encoder->private_->raw_bits_per_partition,
										encoder->private_->lp_coeff[lpc_order-1],
										frame_header->blocksize,
										subframe_bps,
										lpc_order,
										qlp_coeff_precision,
										rice_parameter,
										min_partition_order,
										max_partition_order,
										precompute_partition_sums,
										encoder->protected_->do_escape_coding,
										encoder->protected_->rice_parameter_search_dist,
										subframe[!_best_subframe],
										partitioned_rice_contents[!_best_subframe]
									);
								if(_candidate_bits > 0) { /* if == 0, there was a problem quantizing the lpcoeffs */
									if(_candidate_bits < _best_bits) {
										_best_subframe = !_best_subframe;
										_best_bits = _candidate_bits;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	/* under rare circumstances this can happen when all but lpc subframe types are disabled: */
	if(_best_bits == UINT_MAX) {
		FLAC__ASSERT(_best_subframe == 0);
		_best_bits = evaluate_verbatim_subframe_(integer_signal, frame_header->blocksize, subframe_bps, subframe[_best_subframe]);
	}

	*best_subframe = _best_subframe;
	*best_bits = _best_bits;

	return true;
}

FLAC__bool add_subframe_(
	FLAC__StreamEncoder *encoder,
	const FLAC__FrameHeader *frame_header,
	unsigned subframe_bps,
	const FLAC__Subframe *subframe,
	FLAC__BitBuffer *frame
)
{
	switch(subframe->type) {
		case FLAC__SUBFRAME_TYPE_CONSTANT:
			if(!FLAC__subframe_add_constant(&(subframe->data.constant), subframe_bps, subframe->wasted_bits, frame)) {
				encoder->protected_->state = FLAC__STREAM_ENCODER_FATAL_ERROR_WHILE_ENCODING;
				return false;
			}
			break;
		case FLAC__SUBFRAME_TYPE_FIXED:
			if(!FLAC__subframe_add_fixed(&(subframe->data.fixed), frame_header->blocksize - subframe->data.fixed.order, subframe_bps, subframe->wasted_bits, frame)) {
				encoder->protected_->state = FLAC__STREAM_ENCODER_FATAL_ERROR_WHILE_ENCODING;
				return false;
			}
			break;
		case FLAC__SUBFRAME_TYPE_LPC:
			if(!FLAC__subframe_add_lpc(&(subframe->data.lpc), frame_header->blocksize - subframe->data.lpc.order, subframe_bps, subframe->wasted_bits, frame)) {
				encoder->protected_->state = FLAC__STREAM_ENCODER_FATAL_ERROR_WHILE_ENCODING;
				return false;
			}
			break;
		case FLAC__SUBFRAME_TYPE_VERBATIM:
			if(!FLAC__subframe_add_verbatim(&(subframe->data.verbatim), frame_header->blocksize, subframe_bps, subframe->wasted_bits, frame)) {
				encoder->protected_->state = FLAC__STREAM_ENCODER_FATAL_ERROR_WHILE_ENCODING;
				return false;
			}
			break;
		default:
			FLAC__ASSERT(0);
	}

	return true;
}

unsigned evaluate_constant_subframe_(
	const FLAC__int32 signal,
	unsigned subframe_bps,
	FLAC__Subframe *subframe
)
{
	subframe->type = FLAC__SUBFRAME_TYPE_CONSTANT;
	subframe->data.constant.value = signal;

	return FLAC__SUBFRAME_ZERO_PAD_LEN + FLAC__SUBFRAME_TYPE_LEN + FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN + subframe_bps;
}

unsigned evaluate_fixed_subframe_(
	FLAC__StreamEncoder *encoder,
	const FLAC__int32 signal[],
	FLAC__int32 residual[],
	FLAC__uint32 abs_residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned raw_bits_per_partition[],
	unsigned blocksize,
	unsigned subframe_bps,
	unsigned order,
	unsigned rice_parameter,
	unsigned min_partition_order,
	unsigned max_partition_order,
	FLAC__bool precompute_partition_sums,
	FLAC__bool do_escape_coding,
	unsigned rice_parameter_search_dist,
	FLAC__Subframe *subframe,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents
)
{
	unsigned i, residual_bits;
	const unsigned residual_samples = blocksize - order;

	FLAC__fixed_compute_residual(signal+order, residual_samples, order, residual);

	subframe->type = FLAC__SUBFRAME_TYPE_FIXED;

	subframe->data.fixed.entropy_coding_method.type = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE;
	subframe->data.fixed.entropy_coding_method.data.partitioned_rice.contents = partitioned_rice_contents;
	subframe->data.fixed.residual = residual;

	residual_bits =
		find_best_partition_order_(
			encoder->private_,
			residual,
			abs_residual,
			abs_residual_partition_sums,
			raw_bits_per_partition,
			residual_samples,
			order,
			rice_parameter,
			min_partition_order,
			max_partition_order,
			precompute_partition_sums,
			do_escape_coding,
			rice_parameter_search_dist,
			&subframe->data.fixed.entropy_coding_method.data.partitioned_rice
		);

	subframe->data.fixed.order = order;
	for(i = 0; i < order; i++)
		subframe->data.fixed.warmup[i] = signal[i];

	return FLAC__SUBFRAME_ZERO_PAD_LEN + FLAC__SUBFRAME_TYPE_LEN + FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN + (order * subframe_bps) + residual_bits;
}

unsigned evaluate_lpc_subframe_(
	FLAC__StreamEncoder *encoder,
	const FLAC__int32 signal[],
	FLAC__int32 residual[],
	FLAC__uint32 abs_residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned raw_bits_per_partition[],
	const FLAC__real lp_coeff[],
	unsigned blocksize,
	unsigned subframe_bps,
	unsigned order,
	unsigned qlp_coeff_precision,
	unsigned rice_parameter,
	unsigned min_partition_order,
	unsigned max_partition_order,
	FLAC__bool precompute_partition_sums,
	FLAC__bool do_escape_coding,
	unsigned rice_parameter_search_dist,
	FLAC__Subframe *subframe,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents
)
{
	FLAC__int32 qlp_coeff[FLAC__MAX_LPC_ORDER];
	unsigned i, residual_bits;
	int quantization, ret;
	const unsigned residual_samples = blocksize - order;

	/* try to keep qlp coeff precision such that only 32-bit math is required for decode of <=16bps streams */
	if(subframe_bps <= 16) {
		FLAC__ASSERT(order > 0);
		FLAC__ASSERT(order <= FLAC__MAX_LPC_ORDER);
		qlp_coeff_precision = min(qlp_coeff_precision, 32 - subframe_bps - FLAC__bitmath_ilog2(order));
	}

	ret = FLAC__lpc_quantize_coefficients(lp_coeff, order, qlp_coeff_precision, qlp_coeff, &quantization);
	if(ret != 0)
		return 0; /* this is a hack to indicate to the caller that we can't do lp at this order on this subframe */

	if(subframe_bps + qlp_coeff_precision + FLAC__bitmath_ilog2(order) <= 32)
		if(subframe_bps <= 16 && qlp_coeff_precision <= 16)
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit(signal+order, residual_samples, qlp_coeff, order, quantization, residual);
		else
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients(signal+order, residual_samples, qlp_coeff, order, quantization, residual);
	else
		encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_64bit(signal+order, residual_samples, qlp_coeff, order, quantization, residual);

	subframe->type = FLAC__SUBFRAME_TYPE_LPC;

	subframe->data.lpc.entropy_coding_method.type = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE;
	subframe->data.lpc.entropy_coding_method.data.partitioned_rice.contents = partitioned_rice_contents;
	subframe->data.lpc.residual = residual;

	residual_bits =
		find_best_partition_order_(
			encoder->private_,
			residual,
			abs_residual,
			abs_residual_partition_sums,
			raw_bits_per_partition,
			residual_samples,
			order,
			rice_parameter,
			min_partition_order,
			max_partition_order,
			precompute_partition_sums,
			do_escape_coding,
			rice_parameter_search_dist,
			&subframe->data.fixed.entropy_coding_method.data.partitioned_rice
		);

	subframe->data.lpc.order = order;
	subframe->data.lpc.qlp_coeff_precision = qlp_coeff_precision;
	subframe->data.lpc.quantization_level = quantization;
	memcpy(subframe->data.lpc.qlp_coeff, qlp_coeff, sizeof(FLAC__int32)*FLAC__MAX_LPC_ORDER);
	for(i = 0; i < order; i++)
		subframe->data.lpc.warmup[i] = signal[i];

	return FLAC__SUBFRAME_ZERO_PAD_LEN + FLAC__SUBFRAME_TYPE_LEN + FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN + FLAC__SUBFRAME_LPC_QLP_COEFF_PRECISION_LEN + FLAC__SUBFRAME_LPC_QLP_SHIFT_LEN + (order * (qlp_coeff_precision + subframe_bps)) + residual_bits;
}

unsigned evaluate_verbatim_subframe_(
	const FLAC__int32 signal[],
	unsigned blocksize,
	unsigned subframe_bps,
	FLAC__Subframe *subframe
)
{
	subframe->type = FLAC__SUBFRAME_TYPE_VERBATIM;

	subframe->data.verbatim.data = signal;

	return FLAC__SUBFRAME_ZERO_PAD_LEN + FLAC__SUBFRAME_TYPE_LEN + FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN + (blocksize * subframe_bps);
}

unsigned find_best_partition_order_(
	FLAC__StreamEncoderPrivate *private_,
	const FLAC__int32 residual[],
	FLAC__uint32 abs_residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned raw_bits_per_partition[],
	unsigned residual_samples,
	unsigned predictor_order,
	unsigned rice_parameter,
	unsigned min_partition_order,
	unsigned max_partition_order,
	FLAC__bool precompute_partition_sums,
	FLAC__bool do_escape_coding,
	unsigned rice_parameter_search_dist,
	FLAC__EntropyCodingMethod_PartitionedRice *best_partitioned_rice
)
{
	FLAC__int32 r;
	unsigned residual_bits, best_residual_bits = 0;
	unsigned residual_sample;
	unsigned best_parameters_index = 0;
	const unsigned blocksize = residual_samples + predictor_order;

	/* compute abs(residual) for use later */
	for(residual_sample = 0; residual_sample < residual_samples; residual_sample++) {
		r = residual[residual_sample];
		abs_residual[residual_sample] = (FLAC__uint32)(r<0? -r : r);
	}

	max_partition_order = FLAC__format_get_max_rice_partition_order_from_blocksize_limited_max_and_predictor_order(max_partition_order, blocksize, predictor_order);
	min_partition_order = min(min_partition_order, max_partition_order);

	if(precompute_partition_sums) {
		int partition_order;
		unsigned sum;

		precompute_partition_info_sums_(abs_residual, abs_residual_partition_sums, residual_samples, predictor_order, min_partition_order, max_partition_order);

		if(do_escape_coding)
			precompute_partition_info_escapes_(residual, raw_bits_per_partition, residual_samples, predictor_order, min_partition_order, max_partition_order);

		for(partition_order = (int)max_partition_order, sum = 0; partition_order >= (int)min_partition_order; partition_order--) {
#ifdef DONT_ESTIMATE_RICE_BITS
			if(!
				set_partitioned_rice_with_precompute_(
					residual,
					abs_residual_partition_sums+sum,
					raw_bits_per_partition+sum,
					residual_samples,
					predictor_order,
					rice_parameter,
					rice_parameter_search_dist,
					(unsigned)partition_order,
					do_escape_coding,
					&private_->partitioned_rice_contents_extra[!best_parameters_index],
					&residual_bits
				)
			)
#else
			if(!
				set_partitioned_rice_with_precompute_(
					abs_residual,
					abs_residual_partition_sums+sum,
					raw_bits_per_partition+sum,
					residual_samples,
					predictor_order,
					rice_parameter,
					rice_parameter_search_dist,
					(unsigned)partition_order,
					do_escape_coding,
					&private_->partitioned_rice_contents_extra[!best_parameters_index],
					&residual_bits
				)
			)
#endif
			{
				FLAC__ASSERT(best_residual_bits != 0);
				break;
			}
			sum += 1u << partition_order;
			if(best_residual_bits == 0 || residual_bits < best_residual_bits) {
				best_residual_bits = residual_bits;
				best_parameters_index = !best_parameters_index;
				best_partitioned_rice->order = partition_order;
			}
		}
	}
	else {
		unsigned partition_order;
		for(partition_order = min_partition_order; partition_order <= max_partition_order; partition_order++) {
#ifdef DONT_ESTIMATE_RICE_BITS
			if(!
				set_partitioned_rice_(
					abs_residual,
					residual,
					residual_samples,
					predictor_order,
					rice_parameter,
					rice_parameter_search_dist,
					partition_order,
					&private_->partitioned_rice_contents_extra[!best_parameters_index],
					&residual_bits
				)
			)
#else
			if(!
				set_partitioned_rice_(
					abs_residual,
					residual_samples,
					predictor_order,
					rice_parameter,
					rice_parameter_search_dist,
					partition_order,
					&private_->partitioned_rice_contents_extra[!best_parameters_index],
					&residual_bits
				)
			)
#endif
			{
				FLAC__ASSERT(best_residual_bits != 0);
				break;
			}
			if(best_residual_bits == 0 || residual_bits < best_residual_bits) {
				best_residual_bits = residual_bits;
				best_parameters_index = !best_parameters_index;
				best_partitioned_rice->order = partition_order;
			}
		}
	}

	/*
	 * We are allowed to de-const the pointer based on our special knowledge;
	 * it is const to the outside world.
	 */
	{
		FLAC__EntropyCodingMethod_PartitionedRiceContents* best_partitioned_rice_contents = (FLAC__EntropyCodingMethod_PartitionedRiceContents*)best_partitioned_rice->contents;
		FLAC__format_entropy_coding_method_partitioned_rice_contents_ensure_size(best_partitioned_rice_contents, max(6, best_partitioned_rice->order));
		memcpy(best_partitioned_rice_contents->parameters, private_->partitioned_rice_contents_extra[best_parameters_index].parameters, sizeof(unsigned)*(1<<(best_partitioned_rice->order)));
		memcpy(best_partitioned_rice_contents->raw_bits, private_->partitioned_rice_contents_extra[best_parameters_index].raw_bits, sizeof(unsigned)*(1<<(best_partitioned_rice->order)));
	}

	return best_residual_bits;
}

void precompute_partition_info_sums_(
	const FLAC__uint32 abs_residual[],
	FLAC__uint64 abs_residual_partition_sums[],
	unsigned residual_samples,
	unsigned predictor_order,
	unsigned min_partition_order,
	unsigned max_partition_order
)
{
	int partition_order;
	unsigned from_partition, to_partition = 0;
	const unsigned blocksize = residual_samples + predictor_order;

	/* first do max_partition_order */
	for(partition_order = (int)max_partition_order; partition_order >= 0; partition_order--) {
		FLAC__uint64 abs_residual_partition_sum;
		FLAC__uint32 abs_r;
		unsigned partition, partition_sample, partition_samples, residual_sample;
		const unsigned partitions = 1u << partition_order;
		const unsigned default_partition_samples = blocksize >> partition_order;

		FLAC__ASSERT(default_partition_samples > predictor_order);

		for(partition = residual_sample = 0; partition < partitions; partition++) {
			partition_samples = default_partition_samples;
			if(partition == 0)
				partition_samples -= predictor_order;
			abs_residual_partition_sum = 0;
			for(partition_sample = 0; partition_sample < partition_samples; partition_sample++) {
				abs_r = abs_residual[residual_sample];
				abs_residual_partition_sum += abs_r;
				residual_sample++;
			}
			abs_residual_partition_sums[partition] = abs_residual_partition_sum;
		}
		to_partition = partitions;
		break;
	}

	/* now merge partitions for lower orders */
	for(from_partition = 0, --partition_order; partition_order >= (int)min_partition_order; partition_order--) {
		FLAC__uint64 s;
		unsigned i;
		const unsigned partitions = 1u << partition_order;
		for(i = 0; i < partitions; i++) {
			s = abs_residual_partition_sums[from_partition];
			from_partition++;
			abs_residual_partition_sums[to_partition] = s + abs_residual_partition_sums[from_partition];
			from_partition++;
			to_partition++;
		}
	}
}

void precompute_partition_info_escapes_(
	const FLAC__int32 residual[],
	unsigned raw_bits_per_partition[],
	unsigned residual_samples,
	unsigned predictor_order,
	unsigned min_partition_order,
	unsigned max_partition_order
)
{
	int partition_order;
	unsigned from_partition, to_partition = 0;
	const unsigned blocksize = residual_samples + predictor_order;

	/* first do max_partition_order */
	for(partition_order = (int)max_partition_order; partition_order >= 0; partition_order--) {
		FLAC__int32 r, residual_partition_min, residual_partition_max;
		unsigned silog2_min, silog2_max;
		unsigned partition, partition_sample, partition_samples, residual_sample;
		const unsigned partitions = 1u << partition_order;
		const unsigned default_partition_samples = blocksize >> partition_order;

		FLAC__ASSERT(default_partition_samples > predictor_order);

		for(partition = residual_sample = 0; partition < partitions; partition++) {
			partition_samples = default_partition_samples;
			if(partition == 0)
				partition_samples -= predictor_order;
			residual_partition_min = residual_partition_max = 0;
			for(partition_sample = 0; partition_sample < partition_samples; partition_sample++) {
				r = residual[residual_sample];
				if(r < residual_partition_min)
					residual_partition_min = r;
				else if(r > residual_partition_max)
					residual_partition_max = r;
				residual_sample++;
			}
			silog2_min = FLAC__bitmath_silog2(residual_partition_min);
			silog2_max = FLAC__bitmath_silog2(residual_partition_max);
			raw_bits_per_partition[partition] = max(silog2_min, silog2_max);
		}
		to_partition = partitions;
		break;
	}

	/* now merge partitions for lower orders */
	for(from_partition = 0, --partition_order; partition_order >= (int)min_partition_order; partition_order--) {
		unsigned m;
		unsigned i;
		const unsigned partitions = 1u << partition_order;
		for(i = 0; i < partitions; i++) {
			m = raw_bits_per_partition[from_partition];
			from_partition++;
			raw_bits_per_partition[to_partition] = max(m, raw_bits_per_partition[from_partition]);
			from_partition++;
			to_partition++;
		}
	}
}

#ifdef VARIABLE_RICE_BITS
#undef VARIABLE_RICE_BITS
#endif
#ifndef DONT_ESTIMATE_RICE_BITS
#define VARIABLE_RICE_BITS(value, parameter) ((value) >> (parameter))
#endif

#ifdef DONT_ESTIMATE_RICE_BITS
FLAC__bool set_partitioned_rice_(
	const FLAC__uint32 abs_residual[],
	const FLAC__int32 residual[],
	const unsigned residual_samples,
	const unsigned predictor_order,
	const unsigned suggested_rice_parameter,
	const unsigned rice_parameter_search_dist,
	const unsigned partition_order,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents,
	unsigned *bits
)
#else
FLAC__bool set_partitioned_rice_(
	const FLAC__uint32 abs_residual[],
	const unsigned residual_samples,
	const unsigned predictor_order,
	const unsigned suggested_rice_parameter,
	const unsigned rice_parameter_search_dist,
	const unsigned partition_order,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents,
	unsigned *bits
)
#endif
{
	unsigned rice_parameter, partition_bits;
#ifndef NO_RICE_SEARCH
	unsigned best_partition_bits;
	unsigned min_rice_parameter, max_rice_parameter, best_rice_parameter = 0;
#endif
	unsigned bits_ = FLAC__ENTROPY_CODING_METHOD_TYPE_LEN + FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN;
	unsigned *parameters;

	FLAC__ASSERT(suggested_rice_parameter < FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER);

	FLAC__format_entropy_coding_method_partitioned_rice_contents_ensure_size(partitioned_rice_contents, max(6, partition_order));
	parameters = partitioned_rice_contents->parameters;

	if(partition_order == 0) {
		unsigned i;

#ifndef NO_RICE_SEARCH
		if(rice_parameter_search_dist) {
			if(suggested_rice_parameter < rice_parameter_search_dist)
				min_rice_parameter = 0;
			else
				min_rice_parameter = suggested_rice_parameter - rice_parameter_search_dist;
			max_rice_parameter = suggested_rice_parameter + rice_parameter_search_dist;
			if(max_rice_parameter >= FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER) {
#ifdef DEBUG_VERBOSE
				fprintf(stderr, "clipping rice_parameter (%u -> %u) @2\n", max_rice_parameter, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1);
#endif
				max_rice_parameter = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1;
			}
		}
		else
			min_rice_parameter = max_rice_parameter = suggested_rice_parameter;

		best_partition_bits = 0xffffffff;
		for(rice_parameter = min_rice_parameter; rice_parameter <= max_rice_parameter; rice_parameter++) {
#endif
#ifdef VARIABLE_RICE_BITS
#ifdef FLAC__SYMMETRIC_RICE
			partition_bits = (2+rice_parameter) * residual_samples;
#else
			const unsigned rice_parameter_estimate = rice_parameter-1;
			partition_bits = (1+rice_parameter) * residual_samples;
#endif
#else
			partition_bits = 0;
#endif
			partition_bits += FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN;
			for(i = 0; i < residual_samples; i++) {
#ifdef VARIABLE_RICE_BITS
#ifdef FLAC__SYMMETRIC_RICE
				partition_bits += VARIABLE_RICE_BITS(abs_residual[i], rice_parameter);
#else
				partition_bits += VARIABLE_RICE_BITS(abs_residual[i], rice_parameter_estimate);
#endif
#else
				partition_bits += FLAC__bitbuffer_rice_bits(residual[i], rice_parameter); /* NOTE: we will need to pass in residual[] in addition to abs_residual[] */
#endif
			}
#ifndef NO_RICE_SEARCH
			if(partition_bits < best_partition_bits) {
				best_rice_parameter = rice_parameter;
				best_partition_bits = partition_bits;
			}
		}
#endif
		parameters[0] = best_rice_parameter;
		bits_ += best_partition_bits;
	}
	else {
		unsigned partition, residual_sample, save_residual_sample, partition_sample;
		unsigned partition_samples;
		FLAC__uint64 mean, k;
		const unsigned partitions = 1u << partition_order;
		for(partition = residual_sample = 0; partition < partitions; partition++) {
			partition_samples = (residual_samples+predictor_order) >> partition_order;
			if(partition == 0) {
				if(partition_samples <= predictor_order)
					return false;
				else
					partition_samples -= predictor_order;
			}
			mean = 0;
			save_residual_sample = residual_sample;
			for(partition_sample = 0; partition_sample < partition_samples; residual_sample++, partition_sample++)
				mean += abs_residual[residual_sample];
			residual_sample = save_residual_sample;
#ifdef FLAC__SYMMETRIC_RICE
			mean += partition_samples >> 1; /* for rounding effect */
			mean /= partition_samples;

			/* calc rice_parameter = floor(log2(mean)) */
			rice_parameter = 0;
			mean>>=1;
			while(mean) {
				rice_parameter++;
				mean >>= 1;
			}
#else
			/* calc rice_parameter ala LOCO-I */
			for(rice_parameter = 0, k = partition_samples; k < mean; rice_parameter++, k <<= 1)
				;
#endif
			if(rice_parameter >= FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER) {
#ifdef DEBUG_VERBOSE
				fprintf(stderr, "clipping rice_parameter (%u -> %u) @3\n", rice_parameter, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1);
#endif
				rice_parameter = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1;
			}

#ifndef NO_RICE_SEARCH
			if(rice_parameter_search_dist) {
				if(rice_parameter < rice_parameter_search_dist)
					min_rice_parameter = 0;
				else
					min_rice_parameter = rice_parameter - rice_parameter_search_dist;
				max_rice_parameter = rice_parameter + rice_parameter_search_dist;
				if(max_rice_parameter >= FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER) {
#ifdef DEBUG_VERBOSE
					fprintf(stderr, "clipping rice_parameter (%u -> %u) @4\n", max_rice_parameter, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1);
#endif
					max_rice_parameter = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1;
				}
			}
			else
				min_rice_parameter = max_rice_parameter = rice_parameter;

			best_partition_bits = 0xffffffff;
			for(rice_parameter = min_rice_parameter; rice_parameter <= max_rice_parameter; rice_parameter++) {
#endif
#ifdef VARIABLE_RICE_BITS
#ifdef FLAC__SYMMETRIC_RICE
				partition_bits = (2+rice_parameter) * partition_samples;
#else
				const unsigned rice_parameter_estimate = rice_parameter-1;
				partition_bits = (1+rice_parameter) * partition_samples;
#endif
#else
				partition_bits = 0;
#endif
				partition_bits += FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN;
				save_residual_sample = residual_sample;
				for(partition_sample = 0; partition_sample < partition_samples; residual_sample++, partition_sample++) {
#ifdef VARIABLE_RICE_BITS
#ifdef FLAC__SYMMETRIC_RICE
					partition_bits += VARIABLE_RICE_BITS(abs_residual[residual_sample], rice_parameter);
#else
					partition_bits += VARIABLE_RICE_BITS(abs_residual[residual_sample], rice_parameter_estimate);
#endif
#else
					partition_bits += FLAC__bitbuffer_rice_bits(residual[residual_sample], rice_parameter); /* NOTE: we will need to pass in residual[] in addition to abs_residual[] */
#endif
				}
#ifndef NO_RICE_SEARCH
				if(rice_parameter != max_rice_parameter)
					residual_sample = save_residual_sample;
				if(partition_bits < best_partition_bits) {
					best_rice_parameter = rice_parameter;
					best_partition_bits = partition_bits;
				}
			}
#endif
			parameters[partition] = best_rice_parameter;
			bits_ += best_partition_bits;
		}
	}

	*bits = bits_;
	return true;
}

#ifdef DONT_ESTIMATE_RICE_BITS
FLAC__bool set_partitioned_rice_with_precompute_(
	const FLAC__int32 residual[],
	const FLAC__uint64 abs_residual_partition_sums[],
	const unsigned raw_bits_per_partition[],
	const unsigned residual_samples,
	const unsigned predictor_order,
	const unsigned suggested_rice_parameter,
	const unsigned rice_parameter_search_dist,
	const unsigned partition_order,
	const FLAC__bool search_for_escapes,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents,
	unsigned *bits
)
#else
FLAC__bool set_partitioned_rice_with_precompute_(
	const FLAC__uint32 abs_residual[],
	const FLAC__uint64 abs_residual_partition_sums[],
	const unsigned raw_bits_per_partition[],
	const unsigned residual_samples,
	const unsigned predictor_order,
	const unsigned suggested_rice_parameter,
	const unsigned rice_parameter_search_dist,
	const unsigned partition_order,
	const FLAC__bool search_for_escapes,
	FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents,
	unsigned *bits
)
#endif
{
	unsigned rice_parameter, partition_bits;
#ifndef NO_RICE_SEARCH
	unsigned best_partition_bits;
	unsigned min_rice_parameter, max_rice_parameter, best_rice_parameter = 0;
#endif
	unsigned flat_bits;
	unsigned bits_ = FLAC__ENTROPY_CODING_METHOD_TYPE_LEN + FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN;
	unsigned *parameters, *raw_bits;

	FLAC__ASSERT(suggested_rice_parameter < FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER);

	FLAC__format_entropy_coding_method_partitioned_rice_contents_ensure_size(partitioned_rice_contents, max(6, partition_order));
	parameters = partitioned_rice_contents->parameters;
	raw_bits = partitioned_rice_contents->raw_bits;

	if(partition_order == 0) {
		unsigned i;

#ifndef NO_RICE_SEARCH
		if(rice_parameter_search_dist) {
			if(suggested_rice_parameter < rice_parameter_search_dist)
				min_rice_parameter = 0;
			else
				min_rice_parameter = suggested_rice_parameter - rice_parameter_search_dist;
			max_rice_parameter = suggested_rice_parameter + rice_parameter_search_dist;
			if(max_rice_parameter >= FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER) {
#ifdef DEBUG_VERBOSE
				fprintf(stderr, "clipping rice_parameter (%u -> %u) @5\n", max_rice_parameter, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1);
#endif
				max_rice_parameter = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1;
			}
		}
		else
			min_rice_parameter = max_rice_parameter = suggested_rice_parameter;

		best_partition_bits = 0xffffffff;
		for(rice_parameter = min_rice_parameter; rice_parameter <= max_rice_parameter; rice_parameter++) {
#endif
#ifdef VARIABLE_RICE_BITS
#ifdef FLAC__SYMMETRIC_RICE
			partition_bits = (2+rice_parameter) * residual_samples;
#else
			const unsigned rice_parameter_estimate = rice_parameter-1;
			partition_bits = (1+rice_parameter) * residual_samples;
#endif
#else
			partition_bits = 0;
#endif
			partition_bits += FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN;
			for(i = 0; i < residual_samples; i++) {
#ifdef VARIABLE_RICE_BITS
#ifdef FLAC__SYMMETRIC_RICE
				partition_bits += VARIABLE_RICE_BITS(abs_residual[i], rice_parameter);
#else
				partition_bits += VARIABLE_RICE_BITS(abs_residual[i], rice_parameter_estimate);
#endif
#else
				partition_bits += FLAC__bitbuffer_rice_bits(residual[i], rice_parameter); /* NOTE: we will need to pass in residual[] instead of abs_residual[] */
#endif
			}
#ifndef NO_RICE_SEARCH
			if(partition_bits < best_partition_bits) {
				best_rice_parameter = rice_parameter;
				best_partition_bits = partition_bits;
			}
		}
#endif
		if(search_for_escapes) {
			flat_bits = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN + FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_RAW_LEN + raw_bits_per_partition[0] * residual_samples;
			if(flat_bits <= best_partition_bits) {
				raw_bits[0] = raw_bits_per_partition[0];
				best_rice_parameter = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER;
				best_partition_bits = flat_bits;
			}
		}
		parameters[0] = best_rice_parameter;
		bits_ += best_partition_bits;
	}
	else {
		unsigned partition, residual_sample, save_residual_sample, partition_sample;
		unsigned partition_samples;
		FLAC__uint64 mean, k;
		const unsigned partitions = 1u << partition_order;
		for(partition = residual_sample = 0; partition < partitions; partition++) {
			partition_samples = (residual_samples+predictor_order) >> partition_order;
			if(partition == 0) {
				if(partition_samples <= predictor_order)
					return false;
				else
					partition_samples -= predictor_order;
			}
			mean = abs_residual_partition_sums[partition];
#ifdef FLAC__SYMMETRIC_RICE
			mean += partition_samples >> 1; /* for rounding effect */
			mean /= partition_samples;

			/* calc rice_parameter = floor(log2(mean)) */
			rice_parameter = 0;
			mean>>=1;
			while(mean) {
				rice_parameter++;
				mean >>= 1;
			}
#else
			/* calc rice_parameter ala LOCO-I */
			for(rice_parameter = 0, k = partition_samples; k < mean; rice_parameter++, k <<= 1)
				;
#endif
			if(rice_parameter >= FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER) {
#ifdef DEBUG_VERBOSE
				fprintf(stderr, "clipping rice_parameter (%u -> %u) @6\n", rice_parameter, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1);
#endif
				rice_parameter = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1;
			}

#ifndef NO_RICE_SEARCH
			if(rice_parameter_search_dist) {
				if(rice_parameter < rice_parameter_search_dist)
					min_rice_parameter = 0;
				else
					min_rice_parameter = rice_parameter - rice_parameter_search_dist;
				max_rice_parameter = rice_parameter + rice_parameter_search_dist;
				if(max_rice_parameter >= FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER) {
#ifdef DEBUG_VERBOSE
					fprintf(stderr, "clipping rice_parameter (%u -> %u) @7\n", max_rice_parameter, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1);
#endif
					max_rice_parameter = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER - 1;
				}
			}
			else
				min_rice_parameter = max_rice_parameter = rice_parameter;

			best_partition_bits = 0xffffffff;
			for(rice_parameter = min_rice_parameter; rice_parameter <= max_rice_parameter; rice_parameter++) {
#endif
#ifdef VARIABLE_RICE_BITS
#ifdef FLAC__SYMMETRIC_RICE
				partition_bits = (2+rice_parameter) * partition_samples;
#else
				const unsigned rice_parameter_estimate = rice_parameter-1;
				partition_bits = (1+rice_parameter) * partition_samples;
#endif
#else
				partition_bits = 0;
#endif
				partition_bits += FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN;
				save_residual_sample = residual_sample;
				for(partition_sample = 0; partition_sample < partition_samples; residual_sample++, partition_sample++) {
#ifdef VARIABLE_RICE_BITS
#ifdef FLAC__SYMMETRIC_RICE
					partition_bits += VARIABLE_RICE_BITS(abs_residual[residual_sample], rice_parameter);
#else
					partition_bits += VARIABLE_RICE_BITS(abs_residual[residual_sample], rice_parameter_estimate);
#endif
#else
					partition_bits += FLAC__bitbuffer_rice_bits(residual[residual_sample], rice_parameter); /* NOTE: we will need to pass in residual[] instead of abs_residual[] */
#endif
				}
#ifndef NO_RICE_SEARCH
				if(rice_parameter != max_rice_parameter)
					residual_sample = save_residual_sample;
				if(partition_bits < best_partition_bits) {
					best_rice_parameter = rice_parameter;
					best_partition_bits = partition_bits;
				}
			}
#endif
			if(search_for_escapes) {
				flat_bits = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN + FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_RAW_LEN + raw_bits_per_partition[partition] * partition_samples;
				if(flat_bits <= best_partition_bits) {
					raw_bits[partition] = raw_bits_per_partition[partition];
					best_rice_parameter = FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER;
					best_partition_bits = flat_bits;
				}
			}
			parameters[partition] = best_rice_parameter;
			bits_ += best_partition_bits;
		}
	}

	*bits = bits_;
	return true;
}

unsigned get_wasted_bits_(FLAC__int32 signal[], unsigned samples)
{
	unsigned i, shift;
	FLAC__int32 x = 0;

	for(i = 0; i < samples && !(x&1); i++)
		x |= signal[i];

	if(x == 0) {
		shift = 0;
	}
	else {
		for(shift = 0; !(x&1); shift++)
			x >>= 1;
	}

	if(shift > 0) {
		for(i = 0; i < samples; i++)
			 signal[i] >>= shift;
	}

	return shift;
}

void append_to_verify_fifo_(verify_input_fifo *fifo, const FLAC__int32 * const input[], unsigned input_offset, unsigned channels, unsigned wide_samples)
{
	unsigned channel;

	for(channel = 0; channel < channels; channel++)
		memcpy(&fifo->data[channel][fifo->tail], &input[channel][input_offset], sizeof(FLAC__int32) * wide_samples);

	fifo->tail += wide_samples;

	FLAC__ASSERT(fifo->tail <= fifo->size);
}

void append_to_verify_fifo_interleaved_(verify_input_fifo *fifo, const FLAC__int32 input[], unsigned input_offset, unsigned channels, unsigned wide_samples)
{
	unsigned channel;
	unsigned sample, wide_sample;
	unsigned tail = fifo->tail;

	sample = input_offset * channels;
	for(wide_sample = 0; wide_sample < wide_samples; wide_sample++) {
		for(channel = 0; channel < channels; channel++)
			fifo->data[channel][tail] = input[sample++];
		tail++;
	}
	fifo->tail = tail;

	FLAC__ASSERT(fifo->tail <= fifo->size);
}

FLAC__StreamDecoderReadStatus verify_read_callback_(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], unsigned *bytes, void *client_data)
{
	FLAC__StreamEncoder *encoder = (FLAC__StreamEncoder*)client_data;
	const unsigned encoded_bytes = encoder->private_->verify.output.bytes;
	(void)decoder;

	if(encoder->private_->verify.needs_magic_hack) {
		FLAC__ASSERT(*bytes >= FLAC__STREAM_SYNC_LENGTH);
		*bytes = FLAC__STREAM_SYNC_LENGTH;
		memcpy(buffer, FLAC__STREAM_SYNC_STRING, *bytes);
		encoder->private_->verify.needs_magic_hack = false;
	}
	else {
		if(encoded_bytes == 0) {
			/*
			 * If we get here, a FIFO underflow has occurred,
			 * which means there is a bug somewhere.
			 */
			FLAC__ASSERT(0);
			return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
		}
		else if(encoded_bytes < *bytes)
			*bytes = encoded_bytes;
		memcpy(buffer, encoder->private_->verify.output.data, *bytes);
		encoder->private_->verify.output.data += *bytes;
		encoder->private_->verify.output.bytes -= *bytes;
	}

	return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

FLAC__StreamDecoderWriteStatus verify_write_callback_(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data)
{
	FLAC__StreamEncoder *encoder = (FLAC__StreamEncoder *)client_data;
	unsigned channel;
	const unsigned channels = FLAC__stream_decoder_get_channels(decoder);
	const unsigned blocksize = frame->header.blocksize;
	const unsigned bytes_per_block = sizeof(FLAC__int32) * blocksize;

	for(channel = 0; channel < channels; channel++) {
		if(0 != memcmp(buffer[channel], encoder->private_->verify.input_fifo.data[channel], bytes_per_block)) {
			unsigned i, sample = 0;
			FLAC__int32 expect = 0, got = 0;

			for(i = 0; i < blocksize; i++) {
				if(buffer[channel][i] != encoder->private_->verify.input_fifo.data[channel][i]) {
					sample = i;
					expect = (FLAC__int32)encoder->private_->verify.input_fifo.data[channel][i];
					got = (FLAC__int32)buffer[channel][i];
					break;
				}
			}
			FLAC__ASSERT(i < blocksize);
			FLAC__ASSERT(frame->header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER);
			encoder->private_->verify.error_stats.absolute_sample = frame->header.number.sample_number + sample;
			encoder->private_->verify.error_stats.frame_number = (unsigned)(frame->header.number.sample_number / blocksize);
			encoder->private_->verify.error_stats.channel = channel;
			encoder->private_->verify.error_stats.sample = sample;
			encoder->private_->verify.error_stats.expected = expect;
			encoder->private_->verify.error_stats.got = got;
			encoder->protected_->state = FLAC__STREAM_ENCODER_VERIFY_MISMATCH_IN_AUDIO_DATA;
			return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
		}
	}
	/* dequeue the frame from the fifo */
	for(channel = 0; channel < channels; channel++) {
		memmove(&encoder->private_->verify.input_fifo.data[channel][0], &encoder->private_->verify.input_fifo.data[channel][blocksize], encoder->private_->verify.input_fifo.tail - blocksize);
	}
	encoder->private_->verify.input_fifo.tail -= blocksize;
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void verify_metadata_callback_(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
	(void)decoder, (void)metadata, (void)client_data;
}

void verify_error_callback_(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	FLAC__StreamEncoder *encoder = (FLAC__StreamEncoder*)client_data;
	(void)decoder, (void)status;
	encoder->protected_->state = FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR;
}
