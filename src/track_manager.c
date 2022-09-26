/**************************************************************
 * Copyright (C) 2021 by Chan Russell, Robert                 *
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
#include "tracks.h"

#include <jack/jack.h>

/**************************************************************
 * Macros and defines                                         *
 *************************************************************/

/**************************************************************
 * Data types                                                 *
 *************************************************************/

struct Track
{
  // data buffer - should be an array of samples to allow easier access
  jack_default_audio_sample_t *channel_left;
  jack_default_audio_sample_t *channel_right;
  uint32_t currIdx;     // Current index into samples, range is 0 to sampleIndexEnd
  uint32_t startIdx;    // Start location - assigned to master's current location
  uint32_t endIdx;      // Number of samples for this track - ie track length
  uint32_t maxIdx;      // Max number of samples -- used by malloc
  enum TrackState state;
  bool repeat;          // If track isn't the longest track, we can repeat it:
                        //      if we get to the end of this track but not master track
                        //      we can repeat this track (or part of it) until master track resets
  bool overdub;         // overdubbing is subtle variation of record - may update start or end indices
};

static struct Track *tracks_[NUM_TRACKS];
struct TrackManager {
    
};

static uint32_t max_track_length_ = 0;
static jack_default_audio_sample_t *mute_track_buffer;

/**************************************************************
 * Static functions
 *************************************************************/

// Destroy Track
static void DestroyTrack(struct Track * track) {
  track->currIdx = 0;
  track->startIdx = 0;
  track->endIdx = 0;
  track->maxIdx = 0;
  track->state = TRACK_STATE_OFF;
  track->repeat = false;
  if (track->channel_left != NULL) {
    free(track->channel_left);
  }
  if (track->channel_right != NULL) {
    free(track->channel_right);
  }
}

// Init Track
static void InitTrack(struct Track * track, bool is_stereo) {
  track->currIdx = 0;
  track->startIdx = 0;
  track->endIdx = 0;
  track->maxIdx = max_track_length_;
  track->state = TRACK_STATE_OFF;
  track->repeat = false;
  track->channel_left = (jack_default_audio_sample_t *)calloc(max_num_frames, sizeof(jack_default_audio_sample_t));
  if (is_stereo) {
    track->channel_right = (jack_default_audio_sample_t *)calloc(max_num_frames, sizeof(jack_default_audio_sample_t));
  }
}

static void SetMaxTrackLength(int num_tracks, unsigned long avail_mem, bool is_stereo) {
  max_track_length_ = avail_mem / num_tracks;
  if (is_stereo) {
    max_track_length_ /= 2;
  }
}

// Track indices update handlers based on state
// Change states of track if necessary
/// TODO: all tracks treated same -- update indexes based on state -- treat OFF as PLAY
/// should give consistent timing - data should be zero in those areas
/// should be simpler to manage instead of checking starts and stops
/// Empty tracks will have values of 0

void UpdateIndexPlay(int track, jack_nframes_t nframes) {
  if (tracks_[track]->currIdx + nframes < max_track_length_) {
    tracks_[track]->currIdx += nframes;
  } else {
    tracks_[track]->currIdx = 0;
  }
}

// TODO: Entering record state, caller, set startIdx and endIdx to currIdx
void UpdateIndexRecord(int track, jack_nframes_t nframes) {
  if (tracks_[track]->currIdx + nframes < max_track_length_) {
    tracks_[track]->currIdx += nframes;
    tracks_[track]->endIx += nframes;
  } else {  // Change state and offsets
    // Force track state to PLAY
    tracks_[track]->currIdx = 0;
  }
}

// TODO: Entering repeat state (always from recording), caller, set endIdx to currIdx
void UpdateIndexRepeat(int track, jack_nframes_t nframes) {
  if (tracks_[track]->currIdx + nframes < tracks_[track]->endIdx) {
    tracks_[track]->currIdx += nframes;
  } else {
    tracks_[track]->currIdx = tracks_[track]->startIdx;
  }
}

void UpdateIndexOverdub(int track, jack_nframes_t nframes) {
  if (tracks_[track]->currIdx + nframes < max_track_length_) {
    // is overdubbing extending current track recorded length?
    if (tracks_[track]->currIdx + nframes > tracks_[track]->endIdx) {
      tracks_[track]->endIx += nframes;
    }
    tracks_[track]->currIdx += nframes;
  } else {  // Change state and offsets
    // Change track state to PLAY
    tracks_[track]->currIdx = 0;
  }
}

// determine number of bytes to copy
// pass in nframes from sample, factor frame_offsets for rec and play
uint32_t num_bytes_to_copy(jack_nframes_t nframes) {

}

/// TODO: State machine handler for each track - move from control.c
// handler will - copy data: from supplied buffer to track buffer or vice versa
//                -- if state is MUTE - data is 0
//              - update indexes


/// Track State Handlers - steady state not transition!

/// Copy Data from track buffer to supplied buffer
void GetTrackData(
  int track,
  uint32_t track_index,
  jack_default_audio_sample_t *channel_left,
  jack_default_audio_sample_t *channel_right,
  uint32_t nframes) {

  if (channel_left != NULL && tracks_[track].channel_left != NULL) {
    memcpy(channel_left,
           &tracks_[track].channel_left[track_index],
           nframes);
  }
  if (channel_right != NULL && tracks_[track].channel_right != NULL) {
    memcpy(channel_right,
           &tracks_[track].channel_right[track_index],
           nframes);
  }
}

/// Copy Data to track buffer from supplied buffer
void SetTrackData(
  int track,
  uint32_t track_index,
  jack_default_audio_sample_t *channel_left,
  jack_default_audio_sample_t *channel_right,
  uint32_t nframes) {

  if (channel_left != NULL && tracks_[track].channel_left != NULL) {
    memcpy(&tracks_[track].channel_left[track_index], channel_left, nframes);
  }
  if (channel_right != NULL && tracks_[track].channel_right != NULL) {
    memcpy(&tracks_[track].channel_right[track_index], channel_right, nframes);
  }
}

/// Set Pointers to Buffers of Jack Ports
void SetPointersToJackPortBuffers(
  jack_default_audio_sample_t *inL,
  jack_default_audio_sample_t *outL,
  jack_default_audio_sample_t *inR,
  jack_default_audio_sample_t *outR) {

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
}

/// Track Event Handlers - once Process is called, it will actually do something

void HandleStatePlay(int track, uint32_t nframes) {
  // Copy Data
  jack_default_audio_sample_t *inL, *outL, *inR, *outR;
  SetPointersToJackPortBuffers(intL, outL, inR, outR);
  GetTrackData(
    track,
    tracks_[track].currIdx,
    outL, outR,
    nframes);

  // Update Indices 
  UpdateIndexPlay(track, nframes);

  // Transition state
  tracks_[track].state = TRACK_STATE_PLAYBACK;
  tracks_[track].overdub = false;
  tracks_[track].repeat = false;
}

void HandleStateRepeat(int track, uint32_t nframes) {
  // Copy Data
  GetTrackData(
    track,
    tracks_[track].currIdx,
    outL, outR,
    nframes);

  // Update Indices
  UpdateIndexRepeat(track, nframes);

  // Transition state
  tracks_[track].state = TRACK_STATE_PLAYBACK;
  tracks_[track].repeat = true;
}

void HandleStateRecording(int track, uint32_t nframes) {
  // Copy Data
  jack_default_audio_sample_t *inL, *outL, *inR, *outR;
  SetPointersToJackPortBuffers(intL, outL, inR, outR);
  SetTrackData(
    track,
    tracks_[track].currIdx,
    inL, intR,
    nframes);

  // Update Indices
  UpdateIndexRecord(track, nframes);

  // Transition state
  tracks_[track].state = TRACK_STATE_RECORDING;
}

void HandleStateOverdubbing(int track, uint32_t nframes) {
  // Copy Data
  jack_default_audio_sample_t *inL, *outL, *inR, *outR;
  SetPointersToJackPortBuffers(intL, outL, inR, outR);
  SetTrackData(
    track,
    tracks_[track].currIdx,
    inL, intR,
    nframes);

  // Update Indices
  UpdateIndexOverdub(track, nframes);

  // Transition state
  tracks_[track].state = TRACK_STATE_RECORDING;
  tracks_[track].overdub = true;
}

void HandleStateMute(int track) {
  // Copy Data
  // Update Indices
  UpdateIndexPlay(track, nframes);

  // Transition state
  tracks_[track].state = TRACK_STATE_MUTE;
}

/**************************************************************
 * Public functions
 *************************************************************/

/* Tracks initialization
 * Malloc largest buffer
 * Determine number of samples to support number of tracks - all
 *  must be the same size, watch for non-even division
 * Create list of track pointers based on number of tracks
 * Create track pointer max offset value based on track length
 * Return true if initialization good: malloc successful
 */

bool TrackManagerInit(int num_tracks, bool is_stereo) {
  if (num_tracks > NUM_TRACKS) {
    return false;
  }
  // Determine max track length
  struct sysinfo info;
  int sysinfo(&info);
  
  SetMaxTrackLength(num_tracks, info.freeram, is_stereo);
  /// TODO: What to do if malloc fails? Stop free all? Return limited configuration?
  for (int track = 0; track < num_tracks; track++) {
    InitTrack(tracks_[track], is_stereo);
  }
  mute_track_buffer = (jack_default_audio_sample_t *)calloc(128, sizeof(jack_default_audio_sample_t));
  if (mute_track_buffer == NULL) {
    return false;
  }
  return true;
}

/*
 * Track Control
 */

/// Set next state - do not update anything as Process might be running!
void SetTrackToPlay(int track) {
}

void SetTrackToRepeat(int track) {
}

void SetTrackToRecord(int track) {
}

void SetTrackToOverdub(int track) {
}

// In this state mixdown will substitute zero
void SetTrackToMute(int track) {
}



// Current index functions
// Absolute index
void track_set_current_index(int track, uint32_t index) {
}
void track_set_start_index(int track, uint32_t index) {
}
void track_set_end_index(int track, uint32_t index) {
}

// Negative offset handling required for alignment
void track_set_current_index_relative_offset(int track, uint32_t offset) {
}
void track_set_start_index_ralative_offset(int track, uint32_t offset) {
}
void track_set_end_index_relative_offset(int track, uint32_t offset) {
}

// Adjust track state
void track_set_state(int track, enum TrackState new_state) {
}

enum TrackState GetTrackState(int track) {
}

void track_set_repeat(int track, bool set_repeat) {
}

// Copies data, starting at current index
// update current index
void track_add_data(int track, bool is_left, jack_default_audio_sample_t *src, jack_nframes_t nframes) {
}

// Sets offset into frame for start_recording
void track_recording_start_frame_offset(int track, uint32_t offset) {
}

// Sets offset into frame for end_recording
void track_recording_end_frame_offset(int track, uint32_t offset) {
}











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
    struct Track * track;
    // update master current index
    looper->masterCurrIdx += nframes;
    if (looper->masterCurrIdx > SAMPLE_LIMIT)
    {
        looper->masterCurrIdx = SAMPLE_LIMIT;
    }
    // loop through all potential tracks for a group
    // some tracks may belong to more than one group - but that we only update the active group!
    // TODO with each track, get state use switch case, call update_index_* handler
    while (idx < NUM_TRACKS)
    {
        track = looper->groupedTracks[sg][idx];
        if ( (track != NULL) &&
             (track->state != TRACK_STATE_OFF)) 
        {
            // for playback, we can let currIdx exceed endIdx for a track - mixdown won't mix it
            // if repeating, it will be reset below
            track->currIdx += nframes;

            // if recording/overdubbing we might be increasing endIdx
            if ((st == idx) &&
                ((looper->state == SYSTEM_STATE_CALIBRATION /*OVERDUBBING*/) ||
                (looper->state == SYSTEM_STATE_RECORDING)))
            {
                if (track->currIdx > SAMPLE_LIMIT)
                {
                    track->currIdx = SAMPLE_LIMIT;

                    // Protect ourselves - Stop Recording/Overdubbing!!!!
                    printf("\n\n ** BUFFER FULL - Switch to Playback\n\n");
                    looper->state = SYSTEM_STATE_PLAYBACK;
                }
                // update selected track's endIdx if necessary
                if (track->currIdx > track->endIdx)
                {
                    track->endIdx = track->currIdx;
                }

                // update master track's masterLength if neccessary
                if (track->endIdx > looper->masterLength[sg])
                {
                    looper->masterLength[sg] = track->endIdx;
                }
            }
            else // playback only - ensure we haven't exceeded endIdx
            {
                // if repeat is enabled and at end for current track - reset track's currIdx
                // if masterCurrIdx > masterLength - reset all tracks' currIdx
                // if repeat NOT enabled but at end for current track - leave it - mixdown uses this to ignore track for mixing
                if (track->repeat && (track->currIdx > track->endIdx))
                {
                    track->currIdx = track->startIdx;
                }
                if (looper->masterCurrIdx > looper->masterLength[sg])
                {
                    track->currIdx = (track->repeat) ? track->startIdx : 0;
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
/*
    if (looper->masterLength[sg] >= TRACK_DEBUG_FRAME_COUNT)
    {
//        printf("\n\nTesting done\n\n");
//        printf("quitting\n");
        looper->exitNow = true;
        looper->state = SYSTEM_STATE_PLAYBACK;
    }
*/
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

    // Keep track of previous state so we can capture transitions
    // this will allow us to keep recording until the buffers are in sync
    static enum SystemStates prevSystemState = SYSTEM_STATE_PASSTHROUGH;

    // check for updated state(s)
    // TODO in other file handlering user input, do not modify track info
    //      set offsets into frame instead, which should be copied locally to prevent
    //      any sequence update issues (ie: UI thread sets offset before updateIndex called)
    controlStateCheck();

    // block control state changes while in here!
    looper->controlLocked = true;

    uint32_t byteSize = (looper->rec_frame_delay > 0) ? (nframes - looper->rec_frame_delay) :
                        (looper->play_frame_delay > 0) ? looper->play_frame_delay : nframes;

//    byteSize = nframes; 
    byteSize *= sizeof (jack_default_audio_sample_t);

    uint32_t offset = (looper->rec_frame_delay > 0) ? (looper->rec_frame_delay) * sizeof(jack_default_audio_sample_t) : 0;

//    offset = 0;

    uint8_t sg = looper->selectedGroup;
    uint8_t st = looper->selectedTrack;
    uint32_t trackIdx = 0;

    // TODO move this into seprate handlers
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

    // TODO move this into separate handlers
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
            // If recording to a playing track the track is moved out
            // indices are advanced by 128 frames, so recording must be
            // reset by 128 frames to line up with what was played
            if (getNumActiveTracks() > 1)
            {
                trackIdx -= 128;
            }

            // Overdubbing
            overdub(
                inL + offset,
                &looper->groupedTracks[sg][st]->channelLeft[trackIdx],
                nframes - looper->rec_frame_delay);
            if (inR)
            {
                overdub(
                    inR + offset,
                    &looper->groupedTracks[sg][st]->channelRight[trackIdx],
                    nframes - looper->rec_frame_delay);
            }
            // pass through to mixdown
        }
        case SYSTEM_STATE_RECORDING:
        {
            stopTimer(TIMER_RECORD_START_DELAY);
            if (prevSystemState != looper->state)
            {
                printf("RecDataCopy masterIDX %d, callCounter %d, offset %d, bytes %d\n", looper->masterCurrIdx, looper->callCounter, offset, byteSize);
            }
            // overwrite track
            if (looper->state != SYSTEM_STATE_OVERDUBBING)
            {
                // If only one track, then record starting from index 0
                // If more than one track, then assume playing to the other track(s) so take into account
                // the buffer delay - 2 128frame buffers out and 2 buffers in or 512 frames/samples!
                // Tracks will have to be treated as circular buffers?
                // Corner cases: 
                //   single track: currIdx is 0 - first recording of data
                //   > 1 track: check master track's currIdx, see if started over (4 buffer delay)
                //              - subtracking 4 buffers should be circular, if < 0, sub remainder from master's endIdx
                // ** ^^ work out on blocks first in book!!! 
                trackIdx = looper->groupedTracks[sg][st]->currIdx;
                if (getNumActiveTracks() > 1)
                {
                    // Check if master reset
                    // TODO redo this whole buffer-index thing after much white-boarding
                    if (((int)looper->masterCurrIdx - 4*128) < 0)
                    {
                    }
                    else
                    {
                        trackIdx -= 512;
                    }
                }

                memcpy (
                    &looper->groupedTracks[sg][st]->channelLeft[trackIdx],
                    inL + offset,
                    byteSize);
                if (inR)
                {
                    memcpy (
                        &looper->groupedTracks[sg][st]->channelRight[trackIdx],
                        inR + offset,
                        byteSize);
                }

            }
            // pass through to mixdown
        }
        case SYSTEM_STATE_CALIBRATION:
        {
            if ((looper->state != SYSTEM_STATE_OVERDUBBING) &&
                (looper->state != SYSTEM_STATE_RECORDING))
            {
                trackIdx = looper->tracks[1].currIdx;
printf("PRCal t0idx %d, t1idx %d\n", trackIdx + offset, trackIdx);
                memcpy (
                    &looper->tracks[1].channelLeft[trackIdx],
                    inL + offset,
                    byteSize);
            }
            // pass through to mixdown
        }
        case SYSTEM_STATE_PLAYBACK:
        {
           if ((looper->state == SYSTEM_STATE_PLAYBACK) && (prevSystemState != looper->state))
           {
               printf("Play masterIDX %d, callCounter %d, bytes %d\n", looper->masterCurrIdx, looper->callCounter, byteSize);
           }
           stopTimer(TIMER_RECORD_STOP_DELAY);
            // if we just finished recording, we need to capture the last little bit
            // of data, if playing only we'd miss it - it will likely be part of this
            // buffer (nframes) but not all of it
#if 1
            if (looper->play_frame_delay > 0)
            {   
                trackIdx = looper->groupedTracks[sg][st]->currIdx;
                offset = 0; // want only first few frames
                memcpy (
                    &looper->groupedTracks[sg][st]->channelLeft[trackIdx],
                    inL + offset,
                    byteSize);
                if (inR)
                {
                    memcpy (
                        &looper->groupedTracks[sg][st]->channelRight[trackIdx],
                        inR + offset,
                        byteSize);
                }
                // for output mix, we want full 128 frames so restore byteSize
                byteSize = nframes * sizeof (jack_default_audio_sample_t);
            }
#endif
            // mixdown
            // when mixing - take into account 2 buffer delay otherwise recorded will be behind
/*
            track = looper->groupedTracks[sg][idx];
            if ( (track != NULL) &&
                 (track->currIdx >= track->startIdx) &&
                 //(track->currIdx < track->endIdx) &&
                 (track->state != TRACK_STATE_OFF) &&
                 (track->state != TRACK_STATE_MUTE))
            {

                trackIdx = track->currIdx + sample;
                if (trackIdx <= track->endIdx)
*/


            doMixDown(looper, inL, outL, mixdownLeft, mixdownRight, nframes);
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

    looper->rec_frame_delay = 0;
    looper->play_frame_delay = 0;

    // block control state changes while in here!
    looper->controlLocked = false;

    prevSystemState = looper->state;
    looper->callCounter++;

    stopTimer(TIMER_PLAY_RECORD_DELAY);
    return 0;      
}


