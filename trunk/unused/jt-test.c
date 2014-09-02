/*
	Test APP for JackTransport by Xj <xj@wp.pl>

	gcc -o jt-test.o jt-test.c -ljack -pthread
*/

#include <stdio.h>
#include <signal.h>
#include <math.h>
#include <stdbool.h>
#include <semaphore.h>
#include <jack/jack.h>

int quit = 0;
sem_t sem;

void signal_handler(int signum) {
	quit = 1;
	sem_post ( &sem );
}

static int process_callback ( jack_nframes_t nframes, void* data) {
	sem_post ( &sem );
        return 0;
}

int main(void) {
	jack_client_t* client;
	jack_position_t jp;

	client = jack_client_open("JT TEST", JackNoStartServer, NULL);
	if (!client) return 1;

	jack_set_process_callback ( client, (JackProcessCallback) process_callback, NULL );
	jack_activate(client);

        struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = &signal_handler;
        sigaction(SIGINT, &sa, NULL);

	int32_t last_tick = -1;
	int32_t last_beat = -1;
	while (!quit) {
		sem_wait ( &sem );

		jack_transport_state_t tstate = jack_transport_query (client, &jp);

		if (jp.valid & JackPositionBBT) {
			// Current Beat From Frame
			double ppq = jp.frame_rate * 60 / jp.beats_per_minute;
			double current_beat = jp.frame / ppq;

			// Current Beat From BBT
			double ppqBar = (jp.bar - 1) * jp.beats_per_bar;
			double ppqBeat = jp.beat - 1;
			double ppqTick = (double) jp.tick / jp.ticks_per_beat;

//			if ( last_beat != jp.beat ) {
			if ( last_tick != jp.tick ) {
				printf("Frame: %010d | Bar: %04d | Beat: %d | Tick: %04d | Offset: %04d | CBFF: %4.2f | CBFBBT: %4.2f | PPQ: %4.2f | BPM: %4.2f\n",
					jp.frame, jp.bar, jp.beat, jp.tick, jp.bbt_offset, current_beat, ppqBar + ppqBeat + ppqTick,ppq, jp.beats_per_minute
				);
			}

			last_beat = jp.beat;
			last_tick = jp.tick;
		}
	}

	printf (".. quiting\n");

	jack_deactivate(client);

	jack_client_close (client);

	return 0;
}
