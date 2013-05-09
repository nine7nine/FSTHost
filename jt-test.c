/*
	Test APP for JackTransport by Xj <xj@wp.pl>

	gcc -o jt-test.o jt-test.c -ljack
*/

#include <stdio.h>
#include <signal.h>
#include <math.h>
#include <jack/jack.h>

int quit = 0;

void signal_handler(int signum) {
	quit = 1;
}

int main(void) {
	jack_client_t* client;
	jack_position_t jp;

	
	client = jack_client_open("JT TEST", JackNoStartServer, NULL);
	if (!client) return 1;
	jack_activate(client);

        struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = &signal_handler;
        sigaction(SIGINT, &sa, NULL);

	while (!quit) {
		jack_transport_state_t tstate = jack_transport_query (client, &jp);

		printf("Frame: %010d", jp.frame);
		if (jp.valid & JackPositionBBT) {
			printf(" | Bar: %04d | Beat: %d | Tick: %04d", jp.bar, jp.beat, jp.tick);

			// Current Beat From Frame
			double ppq = jp.frame_rate * 60 / jp.beats_per_minute;
			double current_beat = jp.frame / ppq;
                	printf( " | CBFF: %4.2f" , current_beat );

			// Current Beat From BBT
			double ppqBar = (jp.bar - 1) * jp.beats_per_bar;
			double ppqBeat = jp.beat - 1;
			double ppqTick = (double) jp.tick / jp.ticks_per_beat;
			printf(" | CBFBBT: %4.2f", ppqBar + ppqBeat + ppqTick);
			
		}
		putchar('\n');
		sleep (1);
	}

	printf (".. quiting\n");

	jack_deactivate(client);

	jack_client_close (client);

	return 0;
}
