/**************************************************************
 * Copyright (C) 2017 by Chan Russell, Robert                 *
 *                                                            *
 * Project: Audio Looper                                      *
 *                                                            *
 * This file contains helper functionality, intended for      *
 * diagnostics.                                               *
 *                                                            *
 * Functionality:                                             *
 * - Timers with nonosecond granularity                       *
 *                                                            *
 *                                                            *
 *************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>

#include "local.h"

/**************************************************************
 * Macros and defines                                         *
 *************************************************************/
#define MAX_TIME_SAMPLES    (10)

/**************************************************************
 * Data types                                                 *
 *************************************************************/
struct TimerConstruct
{
    long int start_time;
    long int time_differences[MAX_TIME_SAMPLES];
    long int time_differenceMax;
    int differencesIndex;
    bool started;
};
typedef struct TimerConstruct tc_t;

static struct timespec gettime_now;
static tc_t timers[TIMER_COUNT];

/**************************************************************
 * Static functions
 *************************************************************/

/**************************************************************
 * Public functions
 *************************************************************/

/*
 * Function: startTimer
 * Input: index of timer to start
 * Output: none
 * Description:
 *   Acquire and store current timestamp
 *
 */
void startTimer(uint8_t index)
{
    if (index >= TIMER_COUNT)
    {
        printf("\n!!Invalid Timer Start!\n");
        return;
    }
    clock_gettime(CLOCK_REALTIME, &gettime_now);
    timers[index].start_time = gettime_now.tv_nsec;
    if (timers[index].started)
    {
        printf("\n!! Timer %d already started\n", index);
    }
    timers[index].started = true;
}

/*
 * Function: stopTimer
 * Input: index of timer to stop
 * Output: none
 * Description:
 *   If the timer was started grab the latest timestamp and store it
 *   Update the maximum difference observed and store the last 10 samples
 *
 */
void stopTimer(uint8_t index)
{
    if (index >= TIMER_COUNT)
    {
        printf("\n!!Invalid Timer Start!\n");
        return;
    }
    if (!timers[index].started)
    {
        return;
    }

    clock_gettime(CLOCK_REALTIME, &gettime_now);
    int long time_diff = gettime_now.tv_nsec - timers[index].start_time;
    if (time_diff < 0)
    {
        time_diff += 1000000000; // rolls over every 1 second
    }

    timers[index].started = false;

    timers[index].time_differences[timers[index].differencesIndex] = time_diff;
    timers[index].differencesIndex++;
    if (timers[index].differencesIndex > MAX_TIME_SAMPLES)
    {
        timers[index].differencesIndex = 0;
    }

    timers[index].time_differenceMax = 
        (time_diff > timers[index].time_differenceMax) ? time_diff : timers[index].time_differenceMax;

}

/*
 * Function: printTimers
 * Input: none
 * Output: none
 * Description:
 *   Display the data from the timer tables
 *   Clear the tables
 */
void printTimers(void)
{
    int i = 0;
    int j = 0;
    printf("\n\nTimers\n");

    for (i = 0; i < TIMER_COUNT; i++)
    {
        if (timers[i].time_differenceMax > 0)
        {
            printf("Timer %d\n", i);
            printf("    Max %d ns\n", timers[i].time_differenceMax);
            printf("    Last %d entries\n", MAX_TIME_SAMPLES);
            for (j = 0; j < MAX_TIME_SAMPLES; j++)
            {
                printf("    %d ns\n", timers[i].time_differences[j]);
            }
            printf("\n");
        }
    }

    memset(timers, 0, sizeof(timers));
}

