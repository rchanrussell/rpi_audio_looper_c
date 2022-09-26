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

int groups_of_tracks[NUM_GROUPS][NUM_TRACKS];

/**************************************************************
 * Public function prototypes
 *************************************************************/

// Set all to -1
void InitializeGroups(void) {
  group = 0;
  track = 0;
  for (group = 0; group < NUM_GROUPS; group++) {
    for (track = 0; track < NUM_TRACKS; track++) {
      groups_of_tracks[group][track] = -1;
    }
  }
}

void AddTrackToGroup(int group, int track) {
}

void RemoteTrackFromGroup(int group, int track) {
}

int GetNumberOfActiveTracks(int group) {
}

// Set all tracks in group to muted state
void MuteGroupTracks(int group) {
}

// Clear reset all tracks in group
void ClearGroupTracks(int group) {
}


#endif // groups.h
