/**************************************************************
 * Copyright (C) 2017 by Chan Russell, Robert                 *
 *                                                            *
 * Project: Audio Looper                                      *
 *                                                            *
 * This file contains functionality to update indices and     *
 * copy the data based upon the record or play states         *
 *                                                            *
 * Functionality:                                             *
 * - Update indices during recording and playback             *
 * - Copy data from input buffers to mixdown output buffers   *
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

#include <jack/jack.h>

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

/**
 * Update the indices of each track based upon the active group (don't update if the group isn't active)
 * We are only operating on the active group!
 */
/*
 * Function: updateIndices
 * Input: pointer to the master looper context
 *        number of frames received by Jack server
 * Output: none
 * Description:
 *   Update the indices for the tracks associated with the active group
 *   Updating includes playback and record states and handles the repeat track option
 *
 */
void updateIndices(struct MasterLooper *looper, jack_nframes_t nframes) 
{
    uint8_t sg = looper->selectedGroup;
    uint8_t st = looper->selectedTrack;
    uint8_t idx = 0;

    // update master current index
    looper->masterCurrIdx += nframes;
    if (looper->masterCurrIdx > SAMPLE_LIMIT)
    {
        looper->masterCurrIdx = SAMPLE_LIMIT;
    }
    // loop through all potential tracks for a group
    // some tracks may belong to more than one group - but that we only update the active group!
    while (idx < NUM_TRACKS)
    {

        if ( (looper->groupedTracks[sg][idx] != NULL) &&
             (looper->groupedTracks[sg][idx]->state != TRACK_STATE_OFF)) 
        {
            // for playback, we can let currIdx exceed endIdx for a track - mixdown won't mix it
            // if repeating, it will be reset below
            looper->groupedTracks[sg][idx]->currIdx += nframes;

            // if recording/overdubbing we might be increasing endIdx
            if ((st == idx) &&
                ((looper->state == SYSTEM_STATE_OVERDUBBING) ||
                (looper->state == SYSTEM_STATE_RECORDING)))
            {
                if (looper->groupedTracks[sg][idx]->currIdx > SAMPLE_LIMIT)
                {
                    looper->groupedTracks[sg][idx]->currIdx = SAMPLE_LIMIT;

                    // Protect ourselves - Stop Recording/Overdubbing!!!!
                    printf("\n\n ** BUFFER FULL - Switch to Playback\n\n");
                    looper->state = SYSTEM_STATE_PLAYBACK;
                }
                // update selected track's endIdx if necessary
                if (looper->groupedTracks[sg][idx]->currIdx > looper->groupedTracks[sg][idx]->endIdx)
                {
                    looper->groupedTracks[sg][idx]->endIdx = looper->groupedTracks[sg][idx]->currIdx;
                }

                // update master track's masterLength if neccessary
                if (looper->groupedTracks[sg][idx]->endIdx > looper->masterLength[sg])
                {
                    looper->masterLength[sg] = looper->groupedTracks[sg][idx]->endIdx;
                }
            }
            else // playback only - ensure we haven't exceeded endIdx
            {
                // if repeat is enabled and at end for current track - reset track's currIdx
                // if masterCurrIdx > masterLength - reset all tracks' currIdx
                // if repeat NOT enabled but at end for current track - leave it - mixdown uses this to ignore track for mixing
                if ( looper->groupedTracks[sg][idx]->repeat &&
                    (looper->groupedTracks[sg][idx]->currIdx > looper->groupedTracks[sg][idx]->endIdx))
                {
                    looper->groupedTracks[sg][idx]->currIdx = looper->groupedTracks[sg][idx]->startIdx;
                }
                if (looper->masterCurrIdx > looper->masterLength[sg])
                {
                    looper->groupedTracks[sg][idx]->currIdx = (looper->groupedTracks[sg][idx]->repeat) ? looper->groupedTracks[sg][idx]->startIdx : 0;
                }
            }
        }
        idx++;
    }
    // reset master's current index here, as we needed it above to know if we're resetting all tracks index or not
    if ((looper->state == SYSTEM_STATE_PLAYBACK) && (looper->masterCurrIdx > looper->masterLength[sg]))
    {
        looper->masterCurrIdx = 0;
    }

}

/*
 * Function: playRecord
 * Input: pointer to the master looper context
 *        pointer to the mixdown buffer for the left channel
 *        pointer to the mixdown buffer for the right channel
 *        number of frames Jack received
 * Output: return status, example code simple_client.c returns 0, this was copied here
 * Description:
 *   The process callback for this JACK application is called in a
 *   special realtime thread once for each audio cycle.
 *
 *   Copy data from input buffers to: track if recording or overdubbing
 *                                  : output buffer if bypass
 *   Copy data from mixdown buffers to: output buffer if not in bypass state
 *
 *   Update the indices of all tracks and masterLength depending on state (calls function)
 *
 */
int playRecord (
    struct MasterLooper *looper,
    jack_default_audio_sample_t *mixdownLeft,
    jack_default_audio_sample_t *mixdownRight,
    jack_nframes_t nframes)
{
    startTimer(TIMER_PLAY_RECORD_DELAY);

    // check for updated state(s)
    controlStateCheck();

    // block control state changes while in here!
    looper->controlLocked = true;

    uint32_t byteSize = sizeof (jack_default_audio_sample_t) * nframes;

    uint8_t sg = looper->selectedGroup;
    uint8_t st = looper->selectedTrack;
    uint32_t trackIdx = 0;

	jack_default_audio_sample_t *inL, *outL, *inR, *outR;
	inL = jack_port_get_buffer (looper->input_portL, nframes);
	outL = jack_port_get_buffer (looper->output_portL, nframes);

    // mono devices will use only the left ports
    // if right ports are NULL, do not copy data
    inR = NULL;
    outR = NULL;
    if (looper->input_portR)
    {
	    inR = jack_port_get_buffer (looper->input_portR, nframes);
    }
    if (looper->output_portR)
    {
	    outR = jack_port_get_buffer (looper->output_portR, nframes);
    }

    // Record/Overdub/Playback
    switch(looper->state)
    {
        case SYSTEM_STATE_PASSTHROUGH:
        {
            memcpy (outL, inL, byteSize);
            if((inR == NULL) && (outR)) // mono in, simulated mono out
            {
                memcpy (outR, inL, byteSize);
            }
            else if ((inR) && (outR)) // stereo in, stereo out
            {
                memcpy (outR, inR, byteSize);
            }
            // if mono, out left channel only
            break;
        }
        case SYSTEM_STATE_OVERDUBBING:
        {
            trackIdx = looper->groupedTracks[sg][st]->currIdx;
            // Overdubbing
            overdub(
                inL,
                &looper->groupedTracks[sg][st]->channelLeft[trackIdx],
                nframes);
            if (inR)
            {
                overdub(
                    inR,
                    &looper->groupedTracks[sg][st]->channelRight[trackIdx],
                    nframes);
            }
            // pass through to mixdown
        }
        case SYSTEM_STATE_RECORDING:
        {
            stopTimer(TIMER_RECORD_START_DELAY);

            trackIdx = looper->groupedTracks[sg][st]->currIdx;
            // overwrite track
            if (looper->state != SYSTEM_STATE_OVERDUBBING)
            {
                memcpy (
                    &looper->groupedTracks[sg][st]->channelLeft[trackIdx],
                    inL,
                    byteSize);
                if (inR)
                {
                    memcpy (
                        &looper->groupedTracks[sg][st]->channelRight[trackIdx],
                        inR,
                        byteSize);
                }

            }
            // pass through to mixdown
        }
        case SYSTEM_STATE_PLAYBACK:
        {
            stopTimer(TIMER_RECORD_STOP_DELAY);
            // mixdown
            doMixDown(looper, inL, inR, mixdownLeft, mixdownRight, nframes);
            // output mix
            memcpy (outL, mixdownLeft, byteSize);
            if (inR && outR)
            {
                memcpy (outR, mixdownRight, byteSize);
            }
            if (!inR && outR) // simulate mono
            {
                memcpy (outR, mixdownLeft, byteSize);
            }
            break;
        }
        default:
            break;
    }

    // Update indecies - all playback tracks, recording track, masterLength
    if (looper->state != SYSTEM_STATE_PASSTHROUGH)
    {
        updateIndices(looper, nframes);
    }

    // block control state changes while in here!
    looper->controlLocked = false;

    stopTimer(TIMER_PLAY_RECORD_DELAY);
    return 0;      
}


