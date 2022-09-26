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
/*
 * Function: sumTwoSamples
 * Input: sample channel 1
 *        sample channel 2
 * Output: summation
 * Description:
 *
 */
jack_default_audio_sample_t sumTwoSamples(
    jack_default_audio_sample_t sample1, 
    jack_default_audio_sample_t sample2)
{
    jack_default_audio_sample_t sum = 0;
    sum = sample1 + sample2;
    // use tanh(*p + track[sample]) if > MAX_SAMPLE_VALUE
    if (sum >= MAX_SAMPLE_VALUE)
    {
        sum = tanh(sample1 + sample2);
    }

    return sum;
}
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
    jack_nframes_t nframes)
{
    jack_default_audio_sample_t *p = in;
    jack_default_audio_sample_t sum = 0;
    uint8_t sample;

    for (sample = 0; sample < nframes; sample++)
    {
        sum = sum(TwoChannels(*p, track[sample]);
        track[sample] = sum;
        p++;
    }
}

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
    jack_nframes_t nframes)
{
    jack_default_audio_sample_t track_index;
    jack_default_audio_sample_t sumLeft = 0;
    jack_default_audio_sample_t sumRight = 0;
    uint8_t idx = 0;
    uint8_t sample;

    if !(array_of_track_indexes && trackBuffers && inBufferLeft && inBufferRight &&
         outBufferLeft && outBufferRight)
    {
        return;
    }

    for (sample = 0; sample < nframes; sample++)
    {
        sumLeft = 0;
        sumRight = 0;
        idx = 0;

        // Mixdown playing tracks for the sample offset
        while (idx < number_of_tracks)
        {
            // get starting index from array_of_track_indexes
            // add sample offset as we move through the number of frames to mixdown
            // left track index then right track index, then next track left and right etc..
            track_index = array_of_track_indexes[track_count];
            sumLeft = sumTwoSamples(sumLeft, trackBuffers[track_index + sample]);
            idx++;
            if (inBufferRight)
            {
                track_index = array_of_track_indexes[track_count];
                sumRight = sumTwoSamples(sumRight, trackBuffers[track_index + sample]);
                idx++;
            }
        }

        // Add input buffer to mixdown
        if (inBufferLeft)
        {
            sumLeft = sumTwoSamples(sumLeft, inBufferLeft[sample]);
        }

        if (inBufferRight)
        {
            sumRight = sumTwoSamples(sumRight, inBufferRight[sample]);
        }

        // Output mixdown to output buffer, which Jack/Alsa will later output to audio device
        if (inBufferLeft)
        {
            outBufferLeft[sample] = sumLeft;
        }
        if (inBufferRight)
        {
            outBufferRight[sample] = sumRight;
        }
    } // sample for loop
}


