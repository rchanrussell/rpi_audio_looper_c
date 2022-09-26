/**************************************************************
 * Copyright (C) 2017 by Chan Russell, Robert                 *
 *                                                            *
 * Project: Audio Looper                                      *
 *                                                            *
 * This file contains functions for mixing and overdubbing    *
 *                                                            *
 * Functionality:                                             *
 * - Overdub track with input buffer                          *
 * - Mixdown all tracks on the active group                   *
 *                                                            *
 *                                                            *
 *************************************************************/
#ifndef MIXER_H
#define MIXER_H

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <float.h>

#include "local.h"

/**************************************************************
 * Macros and defines                                         *
 *************************************************************/

/**************************************************************
 * Data types                                                 *
 *************************************************************/

/**************************************************************
 * Static functions
 *************************************************************/

/**************************************************************
 * Public functions
 *************************************************************/

/*
 * Function: overdub
 * Input: pointer to the Jack supplied input data buffer
 *        pointer to the track buffer to overdub
 *        number of frames to overdub
 * Output: none
 * Description:
 *   Overdub the supplied buffer and track, limiting if necessary
 *   This function is applied to one channel, left or right, only
 *
 */
void overdub(
    jack_default_audio_sample_t *in, 
    jack_default_audio_sample_t *track,
    jack_nframes_t nframes);

/*
 * Function: doMixDown
 * Input:
 *        number of tracks to mixdown
 *        pointer to array of track indexes (index into appropriate tracks to mixdown)
 *        pointer to memory block of tracks (treated as double array)
 *        pointer to the Jack supplied input data buffer for left channel
 *        pointer to the Jack supplied input data buffer for the right channel
 *        pointer to the mixdown output data buffer for the left channel
 *        pointer to the mixdown output data buffer for the right channel
 *        number of frames to mixdown
 * Output: none
 * Description:
 *   Mixdown the tracks associated with the active group, limiting if necessary
 */
void doMixDown(
    uint32_t number_of_tracks,
    jack_default_audio_sample_t *array_of_track_indexes,
    jack_default_audio_sample_t *trackBuffers,
    jack_default_audio_sample_t *inBufferLeft,
    jack_default_audio_sample_t *inBufferRight,
    jack_default_audio_sample_t *outBufferLeft,
    jack_default_audio_sample_t *outBufferRight,
    jack_nframes_t nframes);
#endif // MIXER_H
