CFLAGS = -O2 -Wall -g

fhctrl: ../sysex.c fhctrl.c
	gcc $(CFLAGS) -o $@ $^ -ljack -lconfig

clean:
	rm fhctrl

