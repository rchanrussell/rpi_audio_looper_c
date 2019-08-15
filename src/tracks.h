/**************************************************************
 * Copyright (C) 2017 by Chan Russell, Robert                 *
 *                                                            *
 * Project: Audio Looper                                      *
 *                                                            *
 * This file contains shared data types, defines, and         *
 * prototypes                                                 *
 *                                                            *
 *                                                            *
 *************************************************************/

#ifndef LOCAL_H
#define LOCAL_H

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <float.h>
#include <pthread.h>

#include <jack/jack.h>

/**************************************************************
 * Macros and defines                                         *
 *************************************************************/

/**************************************************************
 * Data types                                                 *
 *************************************************************/
enum TrackState
{
    TRACK_STATE_OFF,                // Empty track or available for recording
    TRACK_STATE_PLAYBACK,           // In playback mode
    TRACK_STATE_RECORDING,          // In recording mode
    TRACK_STATE_MUTE                // In mute state, don't mixdown
};

/**************************************************************
 * Public function prototypes
 *************************************************************/

// Tracks initialization
bool TracksInit(int number_of_tracks, uint32_t max_track_size);

/*
 * Track Control
 */

// Current index functions
// Absolute index
void TrackSetCurrentIndex(int track, uint32_t index); 

void TrackSetStartIndex(int track, uint32_t index);

void TrackSetEndIndex(int track, uint32_t index); 


// Negative offset handling required for alignment
// Track should manage repeat itself, nothing else needs to know
// how to deal with it
void TrackSetCurrentIndexRelativeOffset(int track, uint32_t offset);

void TrackSetStartIndexRelativeOffset(int track, uint32_t offset);

void TrackSetEndIndexRelativeOffset(int track, uint32_t offset);


// Adjust track state
void TrackSetState(int track, enum TrackState new_state);

enum TrackState GetTrackState(int track);

void TrackSetRepeat(int track, bool set_repeat);


// Copies data, starting at current index
// update current index
void TrackAddData(int track, bool is_left, jack_default_audio_sample_t *src, jack_nframes_t nframes);


// Sets offset into frame for start_recording
void TrackRecordingStartFrameOffset(int track, uint32_t offset);


// Sets offset into frame for end_recording
void TrackRecordingEndFrameOffset(int track, uint32_t offset);




#endif // local.h
