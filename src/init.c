/**************************************************************
 * Copyright (C) 2017 by Chan Russell, Robert                 *
 *                                                            *
 * Project: Audio Looper                                      *
 *                                                            *
 * This file contains the main application code, Jack         *
 * interface calls, and data contexts                         *
 * This file is based heavily on simple_client.c from Jack    *
 *                                                            *
 * Functionality:                                             *
 * - System initialization                                    *
 * _ Interface into Jack server and calls our handlers        *
 *                                                            *
 *                                                            *
 *************************************************************/

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>

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
jack_client_t *client;

// System context
static struct MasterLooper looper;
static bool shuttingDown;

// Mixdown buffers
static jack_default_audio_sample_t mixdownLeft[128];
static jack_default_audio_sample_t mixdownRight[128];

/**************************************************************
 * Static functions
 *************************************************************/

/**************************************************************
 * Public functions
 *************************************************************/

/*
 * Function: process
 * Input: number of frames to process
 *        void args currently not used
 * Output: status of playRecord function
 * Description:
 *   The process callback for this JACK application is called in a
 *   special realtime thread once for each audio cycle.
 *
 *   This client does nothing more than copy data from its input
 *   port to its output port. It will exit when stopped by 
 *   the user (e.g. using Ctrl-C on a unix-ish operating system)
 *
 *   This is a wrapper, see play_record.c for implementation
 *
 */
int process (jack_nframes_t nframes, void *arg)
{
    stopTimer(TIMER_PROCESS_TO_PROCESS_TIME);
    startTimer(TIMER_PROCESS_TO_PROCESS_TIME);
    return playRecord(&looper, mixdownLeft, mixdownRight, nframes);
}

/*
 * Function: jack_shutdown
 * Input: void args currently not used
 * Output: none
 * Description:
 *   JACK calls this shutdown_callback if the server ever shuts down or
 *   decides to disconnect the client.
 *
 */
void jack_shutdown (void *arg)
{
    shuttingDown = true;

	const char **ports;
	ports = jack_get_ports (looper.client, NULL, NULL,
				JackPortIsPhysical|JackPortIsOutput);
	if (ports == NULL) {
		fprintf(stderr, "no physical capture ports\n");
		exit (1);
	}

    // Establish connection between ports
	if (jack_disconnect (looper.client, ports[0], jack_port_name (looper.input_portL))) {
		fprintf (stderr, "cannot connect input ports\n");
	}
    if ((looper.input_portR) &&
        (jack_disconnect (looper.client, ports[1], jack_port_name (looper.input_portR)))) {
		fprintf (stderr, "cannot connect input ports\n");
	}

	free (ports);
	
	ports = jack_get_ports (looper.client, NULL, NULL,
				JackPortIsPhysical|JackPortIsInput);
	if (ports == NULL) {
		fprintf(stderr, "no physical playback ports\n");
		exit (1);
	}

	if (jack_disconnect (looper.client, jack_port_name (looper.output_portL), ports[0])) {
		fprintf (stderr, "cannot connect output ports\n");
	}
    if ((looper.output_portR) &&
	    (jack_disconnect (looper.client, jack_port_name (looper.output_portR), ports[1]))) {
		fprintf (stderr, "cannot connect output ports\n");
	}

	free (ports);

	exit (1);
}

/*
 * Function: main
 * Input: hardware device used as audio interface, example hw:1,0
 * Output: system return status
 * Description:
 *   Initialize Jack server - setup client, ports, callbacks
 *   Call control initialization process, starting the interface thread
 *   Sleep while exit is not set
 *   On exit, display the timer table data
 *
 */
int main (int argc, char *argv[])
{
	const char **ports;
	const char *client_name = "simple";
	const char *server_name = NULL;
	jack_options_t options = JackNullOption;
	jack_status_t status;

	/* open a client connection to the JACK server */

	looper.client = jack_client_open (client_name, options, &status, server_name);
	if (looper.client == NULL) {
		fprintf (stderr, "jack_client_open() failed, "
			 "status = 0x%2.0x\n", status);
		if (status & JackServerFailed) {
			fprintf (stderr, "Unable to connect to JACK server\n");
		}
		exit (1);
	}

	if (status & JackServerStarted) {
		fprintf (stderr, "JACK server started\n");
	}
	if (status & JackNameNotUnique) {
		client_name = jack_get_client_name(looper.client);
		fprintf (stderr, "unique name `%s' assigned\n", client_name);
	}

	/* tell the JACK server to call `process()' whenever
	   there is work to be done.
	*/

	jack_set_process_callback (looper.client, process, 0);

	/* tell the JACK server to call `jack_shutdown()' if
	   it ever shuts down, either entirely, or if it
	   just decides to stop calling us.
	*/

	jack_on_shutdown (looper.client, jack_shutdown, 0);

	/* display the current sample rate. 
	 */

	printf ("engine sample rate: %" PRIu32 "\n",
		jack_get_sample_rate (looper.client));

	/* create four ports -- left in and out, right in and out */

	looper.input_portL = jack_port_register (looper.client, "inputL",
					 JACK_DEFAULT_AUDIO_TYPE,
					 JackPortIsInput, 0);
	looper.input_portR = jack_port_register (looper.client, "inputR",
					 JACK_DEFAULT_AUDIO_TYPE,
					 JackPortIsInput, 0);
	looper.output_portL = jack_port_register (looper.client, "outputL",
					  JACK_DEFAULT_AUDIO_TYPE,
					  JackPortIsOutput, 0);
	looper.output_portR = jack_port_register (looper.client, "outputR",
					  JACK_DEFAULT_AUDIO_TYPE,
					  JackPortIsOutput, 0);

	if ((looper.input_portL == NULL) || (looper.output_portL == NULL)) {
		fprintf(stderr, "no more JACK ports available\n");
		exit (1);
	}
	if ((looper.input_portR == NULL) || (looper.output_portR == NULL)) {
		fprintf(stderr, "no more JACK ports available\n");
		exit (1);
	}

	/* Tell the JACK server that we are ready to roll.  Our
	 * process() callback will start running now. */

	if (jack_activate (looper.client)) {
		fprintf (stderr, "cannot activate client");
		exit (1);
	}

	/* Connect the ports.  You can't do this before the client is
	 * activated, because we can't make connections to clients
	 * that aren't running.  Note the confusing (but necessary)
	 * orientation of the driver backend ports: playback ports are
	 * "input" to the backend, and capture ports are "output" from
	 * it.
	 */

	ports = jack_get_ports (looper.client, NULL, NULL,
				JackPortIsPhysical|JackPortIsOutput);
	if (ports == NULL) {
		fprintf(stderr, "no physical capture ports\n");
		exit (1);
	}

    // Establish connection between ports
	if (jack_connect (looper.client, ports[0], jack_port_name (looper.input_portL))) {
        looper.input_portL = NULL;
		fprintf (stderr, "cannot connect input ports\n");
	}
    if (jack_connect (looper.client, ports[1], jack_port_name (looper.input_portR))) {
        looper.input_portR = NULL;
		fprintf (stderr, "cannot connect input ports\n");
	}

	free (ports);
	
	ports = jack_get_ports (looper.client, NULL, NULL,
				JackPortIsPhysical|JackPortIsInput);
	if (ports == NULL) {
		fprintf(stderr, "no physical playback ports\n");
		exit (1);
	}

	if (jack_connect (looper.client, jack_port_name (looper.output_portL), ports[0])) {
        looper.output_portL = NULL;
		fprintf (stderr, "cannot connect output ports\n");
	}
	if (jack_connect (looper.client, jack_port_name (looper.output_portR), ports[1])) {
        looper.output_portR = NULL;
		fprintf (stderr, "cannot connect output ports\n");
	}

	free (ports);


    // Set here for testing until passing group via commands
    looper.selectedGroup = 1;

    if (!controlInit(&looper))
    {
      return -1;
    }

    while(!looper.exitNow)
    {
        sleep(2);
    }

    printf("Closing serial port\n");
    sleep(1);
    serialClose(looper.sfd);
    printTimers();

    int i = 0;
    for (i=0; i<looper.tracks[0].pulseIdx; i++)
    {
        printf("Trk %d, idx %d\n",0, looper.tracks[0].pulseIdxArr[i]);
    }
    for (i=0; i<looper.tracks[1].pulseIdx; i++)
    {
        printf("Trk %d, idx %d\n",1, looper.tracks[1].pulseIdxArr[i]);
    }



    int z = 0;
    for (z = 0; z < NUM_TRACKS; z++)
    {
        printf("\nTrack %d EndIdx %d\n\r",z,looper.tracks[z].endIdx);
    }

    printf("Joining thread\n");
    pthread_join(looper.controlTh, NULL);

	jack_client_close (looper.client);
	exit (0);
}
