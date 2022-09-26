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

#ifndef GROUP_MANAGER_H
#define GROUP_MANAGER_H

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

/**************************************************************
 * Public function prototypes
 *************************************************************/

void AddTrackToGroup(int group, int track);

void RemoteTrackFromGroup(int group, int track);

int GetNumberOfActiveTracks(int group);

void MuteGroupTracks(int group);

void ClearGroupTracks(int group);


#endif // GROUP_MANAGER_H
