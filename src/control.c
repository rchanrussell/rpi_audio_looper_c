/**************************************************************
 * Copyright (C) 2017 by Chan Russell, Robert                 *
 *                                                            *
 * Project: Audio Looper                                      *
 *                                                            *
 * This file contains control functionality and user          *
 * interface handling                                         *
 *                                                            *
 * Functionality: (commands are in ASCII)                     *
 * - Record: record a track and assign track to a group       *
 *   rXXgY: command - r, track XX, group Y,                   *
 * - Overdub: overdub on a track                              *
 *   oXX00: command - o, track XX, pad 00                     *
 * - Mute: mute a track                                       *
 *   mXX00: command - m, track XX, pad 00                     *
 * - Unmute: unmute a track                                   *
 *   uXX00: command - u, track XX, pad 00                     *
 * - Play: stop recording and play all tracks on active group *
 *   p0000, pXX00r: command - p, track XX, pad 00,            *
 *       optional r for repeat on, s for repeat off           *
 *       track is only valid when changing repeat status and  *
 *       in playback mode already -- track is ignored if cmd  *
 *       is used to stop recording or overdubbing             *
 * - Track: add a track to a group                            *
 *   tXXgY: command - t, track XX, group Y                    *
 * - Delete: remove a track from a group                      *
 *   dXXgY: command - d, track XX, group Y                    *
 * - Group: set active group                                  *
 *   gY000: command - g, group Y, pad 000                     *
 * - Stop: reset and return to passthrough state              *
 *   s0000: command - s, pad 0000                             *
 * - Quit: stops the looper application and Jack server       *
 *   q0000: command - q, pad 0000                             *
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
#include <poll.h>

#include <jack/jack.h>
#include <wiringPi.h>
#include <wiringSerial.h>

#include "local.h"

/**************************************************************
 * Macros and defines                                         *
 *************************************************************/

/**************************************************************
 * Data types                                                 *
 *************************************************************/
struct ControlContext
{
    uint8_t track;
    uint8_t group;
    uint8_t event;
    bool repeat;
    bool updated;
};

static struct MasterLooper *looper;
static struct ControlContext cc;        // for storing command info until read by record_play

/**************************************************************
 * Static functions
 *************************************************************/

/*
 * Function: startRecording
 * Input: none
 * Output: none
 * Description:
 *   Start recording active track, assign to active group
 *   and handle indices
 *
 */
static void startRecording(void)
{
/*
    looper->tracks[0].endIdx = TRACK_DEBUG_FRAME_COUNT;
    looper->selectedGroup = 1;
    looper->selectedTrack = 1;
    looper->groupedTracks[1][0] = &looper->tracks[0];
    looper->tracks[0].state = TRACK_STATE_PLAYBACK;
    looper->masterLength[1] = TRACK_DEBUG_FRAME_COUNT;
*/
    // Handle case where track is not assigned to the given group
    if (looper->groupedTracks[cc.group][cc.track] == NULL)
    {
        looper->groupedTracks[cc.group][cc.track] = &looper->tracks[cc.track];
    }

    // If numTracks == 0 or selectedTrack is same as new track and numTracks == 1
    // or if we are recording on a new group
    // --> reset master index and length
    if ( (getNumActiveTracks() == 0) ||
         (cc.group != looper->selectedGroup) ||
        ((getNumActiveTracks() == 1) && (looper->selectedTrack == cc.track)) )
    {
        looper->masterCurrIdx = 0;
        looper->masterLength[cc.group] = 0;
    }

    // Recording so reset repeat to false
    looper->tracks[cc.track].repeat = false;

    // reset end, set current and start to the master's current index
    // this prevents user from waiting for looper to restart and remain silent until
    // their desired spot -- downside: doesn't erase earlier recorded stuff
    looper->tracks[cc.track].endIdx = 0;
    looper->tracks[cc.track].currIdx = looper->masterCurrIdx;
    looper->tracks[cc.track].startIdx = looper->masterCurrIdx;
    looper->selectedGroup = cc.group;
    looper->selectedTrack = cc.track;

    looper->tracks[cc.track].state = TRACK_STATE_RECORDING;
    looper->state = SYSTEM_STATE_RECORDING;
    printf("Recording track %d on group %d, frame delay %d\n", cc.track, cc.group, looper->rec_frame_delay);
}

/*
 * Function: startOverdubbing
 * Input: none
 * Output: none
 * Description:
 *   Start overdubbing, does not update any indices because we do not want to lose recorded data
 *
 */
static void startOverdubbing(void)
{
    // Transition to overdub if track was playback mode only
    if (looper->tracks[cc.track].state != TRACK_STATE_PLAYBACK)
    {
        return;
    }

    looper->selectedTrack = cc.track;

    looper->tracks[cc.track].state = TRACK_STATE_RECORDING;
    looper->state = SYSTEM_STATE_OVERDUBBING;
    printf("Overdubbing track %d\n", cc.track);
}

/*
 * Function: stopRecording
 * Input: none
 * Output: none
 * Description:
 *   For the recording track and group, update indexes and set states to playback
 *
 */
static void stopRecording(void)
{
    // prevent user from stopping the recording on a different track and group!
    cc.track = looper->selectedTrack;
    cc.group = looper->selectedGroup;

    // Set repeat
    if (cc.repeat)
    {
        looper->tracks[cc.track].repeat = cc.repeat;
    }

    looper->tracks[cc.track].endIdx = looper->tracks[cc.track].currIdx + looper->play_frame_delay;

    if (looper->masterLength[cc.group] < looper->masterCurrIdx)
    {
        looper->masterLength[cc.group] = looper->masterCurrIdx + looper->play_frame_delay;
        looper->masterCurrIdx = 0;
    }

    looper->state = SYSTEM_STATE_PLAYBACK;
    looper->tracks[cc.track].state = TRACK_STATE_PLAYBACK;
    printf("Playing track %d, frame delay %d\n", cc.track, looper->play_frame_delay);
}

/*
 * Function: stopOverdubbing
 * Input: none
 * Output: none
 * Description:
 *   For the overdubbing track and group, update indexes and set states to playback
 *
 */
static void stopOverdubbing(void)
{
    // prevent user from stopping the recording on a different track and group!
    cc.track = looper->selectedTrack;
    cc.group = looper->selectedGroup;

    // Set repeat
    if (cc.repeat)
    {
        looper->tracks[cc.track].repeat = cc.repeat;
    }

    if (looper->tracks[cc.track].endIdx < looper->tracks[cc.track].currIdx)
    {
        looper->tracks[cc.track].endIdx = looper->tracks[cc.track].currIdx + looper->play_frame_delay;
    }
    if (looper->masterLength[cc.group] < looper->masterCurrIdx)
    {
        looper->masterLength[cc.group] = looper->masterCurrIdx + looper->play_frame_delay;
        looper->masterCurrIdx = 0;
    }
    looper->state = SYSTEM_STATE_PLAYBACK;
    looper->tracks[cc.track].state = TRACK_STATE_PLAYBACK;
    printf("Playing track %d\n", cc.track);
}

/*
 * Function: resetSystem
 * Input: none
 * Output: none
 * Description:
 *   Reset the system, all offsets to 0, track states to off and system state to passthrough
 *
 */
static void resetSystem(void)
{
    // Update system
    int track = 0;
    int group = 0;

    for (group = 0; group < NUM_GROUPS; group++)
    {
        looper->masterLength[group] = 0;
    }
    looper->masterCurrIdx = 0;
    looper->selectedTrack = 0;
    looper->selectedGroup = 0;
    looper->monitoringOff = false;
    looper->controlLocked = false;

    // Update tracks
    for (track = 0; track < NUM_TRACKS; track++)
    {
        looper->tracks[track].state = TRACK_STATE_OFF;
        looper->tracks[track].endIdx = 0;
        looper->tracks[track].currIdx = 0;
        looper->tracks[track].startIdx = 0;
        looper->tracks[track].repeat = false;
        for (group = 0; group < NUM_GROUPS; group++)
        {
            looper->groupedTracks[group][track] = NULL;
        }
    }
    looper->state = SYSTEM_STATE_PASSTHROUGH;
    printf("System reset\n");
}

/*
 * Function: muteTrack
 * Input: none
 * Output: none
 * Description:
 *   Change the selected track to mute
 *
 */
static void muteTrack(void)
{
    // track cannot transition to mute if empty
    if (looper->tracks[cc.track].state == TRACK_STATE_OFF)
    {
        return;
    }
    looper->selectedTrack = cc.track;
    looper->tracks[cc.track].state = TRACK_STATE_MUTE;
}

/*
 * Function: unmuteTrack
 * Input: none
 * Output: none
 * Description:
 *   Change selected track to playback
 *
 */
static void unmuteTrack(void)
{
    // track cannot transition to playback if empty
    if (looper->tracks[cc.track].state == TRACK_STATE_OFF)
    {
        return;
    }
    looper->selectedTrack = cc.track;
    looper->tracks[cc.track].state = TRACK_STATE_PLAYBACK;
}

/*
 * Function: assignTrackToGroup
 * Input: none
 * Output: none
 * Description:
 *   For a given group and track pairing, point it at the track; add track to a gropu
 *
 */
static void assignTrackToGroup(void)
{
    looper->groupedTracks[cc.group][cc.track] = &looper->tracks[cc.track];
    printf("Add track %d to group %d\n", cc.track, cc.group);
}

/*
 * Function: removeTrackFromGroup
 * Input: none
 * Output: none
 * Description:
 *   Remove a track from a group by setting the pointer to NULL
 *
 */
static void removeTrackFromGroup(void)
{
    looper->groupedTracks[cc.group][cc.track] = NULL;
    printf("Remove track %d from group %d\n", cc.track, cc.group);
}

/*
 * Function: setActiveGroup
 * Input: none
 * Output: none
 * Description:
 *   Set tracks and their offsets to start over for the tracks associated with a given group
 *   All tracks not in the selected group are muted
 *   This function is intended to allow a user to switch between verse and chorus, for example
 *
 */
static void setActiveGroup(void)
{
    looper->selectedGroup = cc.group;
    int track = 0;
    for (track = 0; track < NUM_TRACKS; track++)
    {
        if (looper->groupedTracks[looper->selectedGroup][track]->state != TRACK_STATE_OFF)
        {
            looper->tracks[track].state = TRACK_STATE_MUTE;
        }
    }

    for (track = 0; track < NUM_TRACKS; track++)
    {
        if ((looper->groupedTracks[looper->selectedGroup][track]) &&
           (looper->groupedTracks[looper->selectedGroup][track]->state != TRACK_STATE_OFF))
        {
            looper->groupedTracks[looper->selectedGroup][track]->state = TRACK_STATE_PLAYBACK;
            looper->tracks[track].currIdx = (looper->tracks[track].repeat) ? looper->tracks[track].startIdx : 0;            
        }
    }
    looper->masterCurrIdx = 0;
    printf("Setting group to %d\n", looper->selectedGroup);
}

/*
 * Function: updateRepeatStatus
 * Input: none
 * Output: none
 * Description:
 *   Update repeat for a given track - intended for playback state only
 *
 */
static void updateRepeatStatus(void)
{
    looper->selectedTrack = cc.track;

    // Update repeat if changing repeat for the current track
    if (looper->tracks[cc.track].repeat ^ cc.repeat)
    {
        looper->tracks[cc.track].repeat = cc.repeat;
        if (cc.repeat)
        {
            printf("Repeat enabled for track %d\n", cc.track);
        }
        else
        {
            printf("Repeat disabled for track %d\n", cc.track);
        }
    }
}


/*
 * Function: eventHandlerPassthrough
 * Input: system event
 * Output: none
 * Description:
 *   Handle the passed event for the passthrough state
 *
 */
static void eventHandlerPassthrough(event)
{
    switch(event)
    {
        case SYSTEM_EVENT_PASSTHROUGH:               // Do nothing 
            break;
        case SYSTEM_EVENT_RECORD_TRACK:              // System->recording, track->recording - track & group # required
            startRecording();
            break;
        case SYSTEM_EVENT_OVERDUB_TRACK:             // Do nothing
            break;
        case SYSTEM_EVENT_PLAY_TRACK:                // Reset's track's current index to start index and state to play - track # required
            break;
        case SYSTEM_EVENT_MUTE_TRACK:                // Do nothing
            break;
        case SYSTEM_EVENT_UNMUTE_TRACK:              // Do nothing
            break;
        case SYSTEM_EVENT_ADD_TRACK_TO_GROUP:        // Do nothing
            break;
        case SYSTEM_EVENT_REMOVE_TRACK_FROM_GROUP:   // Do nothing
            break;
        case SYSTEM_EVENT_SET_ACTIVE_GROUP:          // Do nothing
            break;
        default:
            break;
    }
}

/*
 * Function: eventHandlerPlayback
 * Input: system event
 * Output: none
 * Description:
 *   Handle the passed event for the playback state
 *
 */
static void eventHandlerPlayback(event)
{
    switch(event)
    {
        case SYSTEM_EVENT_PASSTHROUGH:               // System->passthrough, all tracks->off, all indexes set to 0
            resetSystem();
            break;
        case SYSTEM_EVENT_RECORD_TRACK:              // System->recording, track->recording - track & group # required
            startRecording();
            break;
        case SYSTEM_EVENT_OVERDUB_TRACK:             // System->overdubbing, track->recording - track & group # required
            startOverdubbing();
            break;
        case SYSTEM_EVENT_PLAY_TRACK:                // Update repeat status for passed track
            updateRepeatStatus();
            break;
        case SYSTEM_EVENT_MUTE_TRACK:                // Place particular track into Mute state - track # required
            muteTrack();
            break;
        case SYSTEM_EVENT_UNMUTE_TRACK:              // Changes track to Play state - track # required
            unmuteTrack();
            break;
        case SYSTEM_EVENT_ADD_TRACK_TO_GROUP:        // Adds a track to a group - nothing more - track # & group # required
            assignTrackToGroup();
            break;
        case SYSTEM_EVENT_REMOVE_TRACK_FROM_GROUP:   // Removes track from a group - track # & group # required
            removeTrackFromGroup();
            break;
        case SYSTEM_EVENT_SET_ACTIVE_GROUP:          // Sets the currently active group - group # required
            setActiveGroup();
            break;
        default:
            break;
    }
}

/*
 * Function: eventHandlerRecording
 * Input: system event
 * Output: none
 * Description:
 *   Handle the passed event for the recording state
 *
 */
static void eventHandlerRecording(event)
{
    switch(event)
    {
        case SYSTEM_EVENT_PASSTHROUGH:               // System->passthrough, all tracks->off, all indexes set to 0
            resetSystem();
            break;
        case SYSTEM_EVENT_RECORD_TRACK:              // Do nothing
            break;
        case SYSTEM_EVENT_OVERDUB_TRACK:             // Do nothing
            break;
        case SYSTEM_EVENT_PLAY_TRACK:                // Reset's track's current index to start index and state to play - track # required
            stopRecording();
            break;
        case SYSTEM_EVENT_MUTE_TRACK:                // Do nothing
            break;
        case SYSTEM_EVENT_UNMUTE_TRACK:              // Do nothing
            break;
        case SYSTEM_EVENT_ADD_TRACK_TO_GROUP:        // Do nothing
            break;
        case SYSTEM_EVENT_REMOVE_TRACK_FROM_GROUP:   // Do nothing
            break;
        case SYSTEM_EVENT_SET_ACTIVE_GROUP:          // Do nothing
            break;
        default:
            break;
    }
}

/*
 * Function: eventHandlerOverdubbing
 * Input: system event
 * Output: none
 * Description:
 *   Handle the passed event for the overdubbing state
 *
 */
static void eventHandlerOverdubbing(event)
{
    switch(event)
    {
        case SYSTEM_EVENT_PASSTHROUGH:               // System->passthrough, all tracks->off, all indexes set to 0
            resetSystem();
            break;
        case SYSTEM_EVENT_RECORD_TRACK:              // Do nothing
            break;
        case SYSTEM_EVENT_OVERDUB_TRACK:             // Do nothing
            break;
        case SYSTEM_EVENT_PLAY_TRACK:                // Reset's track's current index to start index and state to play - track # required
            stopOverdubbing();
            break;
        case SYSTEM_EVENT_MUTE_TRACK:                // Do nothing
            break;
        case SYSTEM_EVENT_UNMUTE_TRACK:              // Do nothing
            break;
        case SYSTEM_EVENT_ADD_TRACK_TO_GROUP:        // Do nothing
            break;
        case SYSTEM_EVENT_REMOVE_TRACK_FROM_GROUP:   // Do nothing
            break;
        case SYSTEM_EVENT_SET_ACTIVE_GROUP:          // Do nothing
            break;
        default:
            break;
    }
}

/*
 * Function: controlStateMachine
 * Input: system event
 * Output: none
 * Description:
 *   Pass event to appropriate handler based on the current system state
 *
 */
static void controlStateMachine(uint8_t event)
{
    switch(looper->state)
    {
        case SYSTEM_STATE_PASSTHROUGH:       // No mixdown or recording
            eventHandlerPassthrough(event);
            break;
        case SYSTEM_STATE_PLAYBACK:          // Tracks available for mixing and playing
            eventHandlerPlayback(event);
            break;
        case SYSTEM_STATE_RECORDING:         // Copying data to selected track
            eventHandlerRecording(event);
            break;
        case SYSTEM_STATE_OVERDUBBING:       // Overdubbing selected track            break;
            eventHandlerOverdubbing(event);
            break;
        default:
            break;
    }
}

/*
 * Function: processUART
 * Input: character buffer from UART
 * Output: none
 * Description:
 *   Processing the UART buffer for 5 characters plus either 'r' for repeat or
 *   carriage return, char 13.
 *   Commands are processed and data, track or group, is checked and changes stored
 *   in the static struct for control context
 *
 */
static void processUART(char buf[])
{
    bool invalidData = false;

    if ((looper->min_serial_data_length >= MIN_SERIAL_DATA_LENGTH) &&
        (buf[SERIAL_LAST_CHAR] != 13) &&
        (buf[SERIAL_LAST_CHAR] != SERIAL_CMD_OPTION_REPEAT_ON) &&
        (buf[SERIAL_LAST_CHAR] != SERIAL_CMD_OPTION_REPEAT_OFF))
    {
        printf("Invalid last char\n");
        serialFlush(looper->sfd);
        return;
    }

    switch(buf[SERIAL_CMD_OFFSET])
    {
        case SERIAL_CMD_OVERDUB_LC:
        case SERIAL_CMD_OVERDUB_UC:
            cc.event = SYSTEM_EVENT_OVERDUB_TRACK;
            cc.track = (buf[SERIAL_TRACK_UPPER_DIGIT] - 48) * 10;
            cc.track += (buf[SERIAL_TRACK_LOWER_DIGIT] - 48);
            break;
        case SERIAL_CMD_RECORD_LC:
        case SERIAL_CMD_RECORD_UC:
            if ((buf[SERIAL_SUB_CMD_OFFSET] == SERIAL_CMD_GROUP_SELECT_LC) ||
                (buf[SERIAL_SUB_CMD_OFFSET] == SERIAL_CMD_GROUP_SELECT_UC))
            {
                printf("Recording CC %d\n",looper->callCounter);
                cc.event = SYSTEM_EVENT_RECORD_TRACK;
                cc.track = (buf[SERIAL_TRACK_UPPER_DIGIT] - 48) * 10;
                cc.track += (buf[SERIAL_TRACK_LOWER_DIGIT] - 48);
                cc.group = (buf[SERIAL_TRACK_GROUP_LOWER_DIGIT] - 48);
            }
            break;
        case SERIAL_CMD_TRACK_MUTE_LC: // set track to mute
        case SERIAL_CMD_TRACK_MUTE_UC:
            cc.event = SYSTEM_EVENT_MUTE_TRACK;
            cc.track = (buf[SERIAL_TRACK_UPPER_DIGIT] - 48) * 10;
            cc.track += (buf[SERIAL_TRACK_LOWER_DIGIT] - 48);
            break;
        case SERIAL_CMD_TRACK_UNMUTE_LC: // set track to play
        case SERIAL_CMD_TRACK_UNMUTE_UC:
            cc.event = SYSTEM_EVENT_UNMUTE_TRACK;
            cc.track = (buf[SERIAL_TRACK_UPPER_DIGIT] - 48) * 10;
            cc.track += (buf[SERIAL_TRACK_LOWER_DIGIT] - 48);
            break;
        case SERIAL_CMD_ADD_TRACK2GROUP_LC: // add track to group
        case SERIAL_CMD_ADD_TRACK2GROUP_UC:
            if ((buf[SERIAL_SUB_CMD_OFFSET] == SERIAL_CMD_GROUP_SELECT_LC) ||
                (buf[SERIAL_SUB_CMD_OFFSET] == SERIAL_CMD_GROUP_SELECT_UC))
            {
                cc.event = SYSTEM_EVENT_ADD_TRACK_TO_GROUP;
                cc.track = (buf[SERIAL_TRACK_UPPER_DIGIT] - 48) * 10;
                cc.track += (buf[SERIAL_TRACK_LOWER_DIGIT] - 48);
                cc.group = (buf[SERIAL_TRACK_GROUP_LOWER_DIGIT] - 48);
            }
            break;
        case SERIAL_CMD_RMV_TRACK_GROUP_LC: // remove track from group
        case SERIAL_CMD_RMV_TRACK_GROUP_UC:
            if ((buf[SERIAL_SUB_CMD_OFFSET] == SERIAL_CMD_GROUP_SELECT_LC) ||
                (buf[SERIAL_SUB_CMD_OFFSET] == SERIAL_CMD_GROUP_SELECT_UC))
            {
                cc.event = SYSTEM_EVENT_REMOVE_TRACK_FROM_GROUP;
                cc.track = (buf[SERIAL_TRACK_UPPER_DIGIT] - 48) * 10;
                cc.track += (buf[SERIAL_TRACK_LOWER_DIGIT] - 48);
                cc.group = (buf[SERIAL_TRACK_GROUP_LOWER_DIGIT] - 48);
            }
            break;
        case SERIAL_CMD_GROUP_SELECT_LC: // select active group
        case SERIAL_CMD_GROUP_SELECT_UC:
            cc.event = SYSTEM_EVENT_SET_ACTIVE_GROUP;
            cc.group = (buf[SERIAL_GROUP_SELECT_LOWER_DIGIT] - 48);
            break;
        case SERIAL_CMD_PLAY_LC: // set system to play
        case SERIAL_CMD_PLAY_UC:
            cc.event = SYSTEM_EVENT_PLAY_TRACK;
            printf("Playing CC %d\n", looper->callCounter);
            if (buf[SERIAL_LAST_CHAR] == SERIAL_CMD_OPTION_REPEAT_ON)
            {
                cc.track = (buf[SERIAL_TRACK_UPPER_DIGIT] - 48) * 10;
                cc.track += (buf[SERIAL_TRACK_LOWER_DIGIT] - 48);
                cc.repeat = true;
            }
            if (buf[SERIAL_LAST_CHAR] == SERIAL_CMD_OPTION_REPEAT_OFF)
            {
                cc.track = (buf[SERIAL_TRACK_UPPER_DIGIT] - 48) * 10;
                cc.track += (buf[SERIAL_TRACK_LOWER_DIGIT] - 48);
                cc.repeat = false;
            }
            break;
        case SERIAL_CMD_SYSTEM_RESET_LC: // set system to passthrough
        case SERIAL_CMD_SYSTEM_RESET_UC:
            cc.track = 0;
            cc.group = 0;
            cc.event = SYSTEM_EVENT_PASSTHROUGH;
            break;
        case SERIAL_CMD_QUIT_LC: // exit application
        case SERIAL_CMD_QUIT_UC:
            printf("quitting\n");
            looper->exitNow = true;
            break;
        default:
            invalidData = true;
            break;
    }

    if ((cc.track >= 0) && (cc.track < NUM_TRACKS) &&
        (cc.group >= 0) && (cc.group < NUM_GROUPS) &&
        (invalidData == false))    
    {
        cc.updated = true;
        serialPutchar(looper->sfd, SERIAL_CMD_ACCEPTED);
    }
    else
    {
        printf("\n** Invalid Cmd or Cmd args\n");
        serialPutchar(looper->sfd, SERIAL_CMD_REJECTED);
    }

    serialFlush(looper->sfd);
}

/*
 * Function:  controlThread
 * Input: none
 * Output: none
 * Description:
 *   Main control thread to monitor user input interfaces
 *   Copy any data to the buffer for processing by the main thread
 *
 */
static void *controlThread(void *arg)
{
    struct pollfd fds[1];
    fds[0].fd = looper->sfd;
    fds[0].events = POLLIN;
    int timeout = 20 * 1000; // check for exit every 1 sec
    int rc;
    int byte = 0;
    char buf[] = {0,0,0,0,0,0};
    serialFlush(looper->sfd);
    while(!looper->exitNow)
    {
        rc = poll(fds, 1, timeout);
        if (rc == -1)
        {
            printf("Poll error\n");
        }
        if ((rc > 0) && (fds[0].revents & POLLIN))
        {
            buf[byte] = serialGetchar(looper->sfd);
            byte++;
            if (byte == looper->min_serial_data_length)
            {
                if ((buf[0] == 'r') || (buf[0] == 'R') || (buf[0] == 'o') || (buf[0] == 'O'))
                {
                    looper->rec_frame_delay = jack_frames_since_cycle_start(looper->client);
                    startTimer(TIMER_RECORD_START_DELAY);
                }
                if (((buf[0] == 'p') || (buf[0] == 'P')) && (looper->state == SYSTEM_STATE_RECORDING))
                {
                    looper->play_frame_delay = jack_frames_since_cycle_start(looper->client);
                    startTimer(TIMER_RECORD_STOP_DELAY);
                }

                startTimer(TIMER_UART_PROCESS);
                processUART(buf);
                stopTimer(TIMER_UART_PROCESS);
                byte = 0;
            }
        }
    }

    printf("control thread exiting\n");
    pthread_exit(NULL);
}


/**************************************************************
 * Public functions
 *************************************************************/

/*
 * Function: controlStateCheck
 * Input: none
 * Output: none
 * Description:
 *   A public interface for the main process to determine if the control state has changed
 *   and if changed, process the changes
 *
 */
void controlStateCheck(void)
{
    if (cc.updated)
    {
        controlStateMachine(cc.event);
        cc.updated = false;
    }
}

/*
 * Function: controlInit
 * Input: pointer to the master looper context
 * Output: pass/fail of init process
 * Description:
 *   Intialize the serial port, create the serial monitoring thread
 *
 */
bool controlInit(struct MasterLooper *mLooper)
{
    looper = mLooper;

    if (wiringPiSetup() == -1)
    {
        printf("WiringPiSetup failed\n");
        return false;
    }

    looper->sfd = serialOpen("/dev/ttyAMA0",  115200);
    if (looper->sfd < 0)
    {
        printf("Error setting up serial port\n");
        return false;
    }
    serialFlush(looper->sfd);
    looper->min_serial_data_length = MIN_SERIAL_DATA_LENGTH;

    // Setup interface monitoring thread
    int rc;
    if ((rc = pthread_create(&looper->controlTh, NULL, controlThread, NULL)))
    {
        printf("Error: pthread_create, rc: %d\n", rc);
        return false;
    }
/*
    // Testing for offset managment -- sync track 1 to track 0
    int j = 0;
    for (j=0; j<TRACK_DEBUG_FRAME_COUNT; j++)
      looper->tracks[0].channelLeft[j] = FLT_MAX;

    looper->tracks[0].endIdx = TRACK_DEBUG_FRAME_COUNT;
    looper->selectedGroup = 1;
    looper->selectedTrack = 1;
    looper->groupedTracks[1][0] = &looper->tracks[0];
    looper->tracks[0].state = TRACK_STATE_PLAYBACK;
    looper->masterLength[1] = TRACK_DEBUG_FRAME_COUNT;
    looper->state = SYSTEM_STATE_PLAYBACK;
*/
//    looper->groupedTracks[0][1] = &looper->tracks[1];
//    looper->tracks[1].state = TRACK_STATE_RECORDING;
//    looper->state = SYSTEM_STATE_CALIBRATION;

    return true;
}

/*
 * Function: getNumActiveTracks
 * Input: none
 * Output: number of active tracks, regardless of group
 * Description:
 *   Determine the number of tracks that have recorded data, endIdx is 0 if track is empty or reset
 *
 */
int getNumActiveTracks(void)
{
    int track = 0;
    int activeTracks = 0;
    for (track = 0; track < NUM_TRACKS; track++)
    {
        if (looper->groupedTracks[looper->selectedGroup][track]->endIdx > 0)
        {
            activeTracks++;
        }
    }
    return activeTracks;
}


