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
        sum = *p + track[sample];
        if (sum > 0.9 * FLT_MAX)
        {
            sum *= 0.9;
        }
        track[sample] = sum;
        p++;
    }
}

/*
 * Function: doMixDown
 * Input: pointer to the Jack supplied input data buffer for left channel
 *        pointer to the Jack supplied input data buffer for the right channel
 *        pointer to the mixdown output data buffer for the left channel
 *        pointer to the mixdown output data buffer for the right channel
 *        number of frames to overdub
 * Output: none
 * Description:
 *   Mixdown the tracks associated with the active group, limiting if necessary
 *   Do not blindly mixdown based upon activeTracks because this destroys grouping ability
 *   Focus on track state of Play or Mute
 *   - GroupNumber updates via control handling will update the individual track's P or M status
 *
 */
void doMixDown(
    struct MasterLooper *looper,
    jack_default_audio_sample_t *inBufferLeft,
    jack_default_audio_sample_t *inBufferRight,
    jack_default_audio_sample_t *mixdownBufferLeft,
    jack_default_audio_sample_t *mixdownBufferRight,
    jack_nframes_t nframes)
{
    uint32_t trackIdx;
    jack_default_audio_sample_t sumLeft = 0;
    jack_default_audio_sample_t sumRight = 0;
    uint8_t sg = looper->selectedGroup;
    uint8_t idx = 0;
    uint8_t sample;
    struct Track * track;
    for (sample = 0; sample < nframes; sample++)
    {
        sumLeft = 0;
        sumRight = 0;
        idx = 0;

        // loop through all potential tracks in selected group for given sample
        // some groups may contain same tracks (ie same drum track for group 1 and 2
        // check track states as some tracks may be muted or off/empty/erased
        // tracks can move groups - NULL will be assigned for the former group if track moves
        while (idx < NUM_TRACKS)
        {
            track = looper->groupedTracks[sg][idx];
            if ( (track != NULL) &&
                 (track->currIdx >= track->startIdx) &&
                 (track->currIdx < track->endIdx) &&
                 (track->state != TRACK_STATE_OFF) &&
                 (track->state != TRACK_STATE_MUTE))
            {

                trackIdx = track->currIdx + sample;
                if (trackIdx <= track->endIdx)
                {
if (track->channelLeft[trackIdx] == FLT_MAX)
{
    if (track->pulseIdx < 7)
      track->pulseIdxArr[track->pulseIdx++] = trackIdx;
}
if ((idx != 0) && track->channelLeft[trackIdx] > (0.001 * FLT_MAX))
{
    printf("T%d idx %d\n", idx, trackIdx);
}
                    sumLeft += track->channelLeft[trackIdx];
                    if (sumLeft > 0.9 * FLT_MAX)
                    {
                        sumLeft *= 0.9;
                    }

                    sumRight += track->channelRight[trackIdx];
                    if (sumRight > 0.9 * FLT_MAX)
                    {
                        sumRight *= 0.9;
                    }
                }
            }
            idx++;
        }
        if (inBufferLeft)
        {
            sumLeft += inBufferLeft[sample];
            if (sumLeft > 0.9 * FLT_MAX)
            {
                sumLeft *= 0.9;
            }
        }

        if (inBufferRight)
        {
            sumRight += inBufferRight[sample];
            if (sumRight > 0.9 * FLT_MAX)
            {
                sumRight *= 0.9;
            }
        }

        mixdownBufferLeft[sample] = sumLeft;
        mixdownBufferRight[sample] = sumRight;
    }
}


