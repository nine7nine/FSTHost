CFLAGS = -O2 -Wall -g

.PHONY: clean

fhctrl: ../sysex.c fhctrl.c
	gcc $(CFLAGS) -g -o $@ $^ -ljack -lconfig

clean:
	rm -f fhctrl
