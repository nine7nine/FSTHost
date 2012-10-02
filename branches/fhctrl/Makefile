CFLAGS = -O2 -Wall -g

.PHONY: clean

fhctrl: ../../trunk/sysex.c nfhc.c fhctrl.c
	$(CC) $(CFLAGS) -g -o $@ $^ -ljack -lconfig -lcdk -lcurses -pthread

clean:
	rm -f fhctrl nfhc
