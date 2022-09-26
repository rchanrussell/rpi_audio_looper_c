/**************************************************************
 * Copyright (C) 20w1 by Chan Russell, Robert                 *
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
    TRACK_STATE_OVERDUB,            // In overdub mode, may/may not update start/end indicies
    TRACK_STATE_PLAYBACK,           // In playback mode
    TRACK_STATE_RECORDING,          // In recording mode - overwrites any previous recording info
    TRACK_STATE_MUTE                // In mute state, don't mixdown
};

// List of pointers to tracks
// If not used/muted/on another group - NULL
// Include L and R? or just is_stereo flag and Mixer assume L then R then L then R?

typedef jack_default_audio_sample_t* TracksCurrentBufferList[MAX_NUMBER_OF_TRACKS];

/**************************************************************
 * Public function prototypes
 *************************************************************/

/* Tracks initialization
 * @param[in]: number_of_tracks - number of tracks to be created
 * @param[in]: is_stereo - determine to create one or two data buffers
 *             per track
 * @return: true if initialization good: malloc successful
 */
bool TrackManagerInit(int number_of_tracks, bool is_stereo);

// Get track states from main system - update states of all tracks
//     update active_track for faster processing
// Get port(s) for input from main system - use _is_stereo if need L and R input
// If active track is recording:
//     use jack_port_get_buffer (port_info, n_frames) to get buffer then
//     use memcpy to direct it in to the local track buffer
// ** output_port will be for mixer - it should request from main system
// Return: set passed array of left (mono) - NULL if muted/not used
//         set passed array of right -- all NULL if mono, NULL if muted/not used
TrackManagerUpdateTracks();

/* Retreive up to two lists to mixdown, NULL means replace value with zero
 * @param[in/out] pointer to left_buffer_pointer_list, 16 tracks
 * @param[in/out] pointer to right_buffer_pointer_list, 16 tracks
TrackManagerReturnPointerToTracksToMix();

TrackManagerResetAllTracks();


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
