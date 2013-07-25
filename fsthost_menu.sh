#!/bin/sh

# FSTHost menu in dialog
#   by Xj <xj@wp.pl>

PATH=.:$PATH

ZENITY=0
ZENITY_WIDTH=420
ZENITY_HEIGHT=700
MODE='' # Will be set later

show_menu() {
	echo "$L" |
	{
	if [ $ZENITY -eq 1 ]; then
		cut -sd '|' -f 1-3 |
		tr '|' '\n' |
		zenity --title 'Wybierz Host' \
			--width=$ZENITY_WIDTH \
			--height=$ZENITY_HEIGHT \
			--list \
			--column 'Number' \
			--column 'Plugin' \
			--column 'Arch'
	else
		awk -F'|' '{print $1 " " "\"" $2 "\""}' |
		xargs dialog --menu 'Select VST' 0 0 0 3>&1 1>&2 2>&3
	fi
	}
}

main_loop() {
	G=$(show_menu)
	[ -z "$G" ] && exit
	
	P=$(echo "$L" | grep "^$G|" | cut -sd'|' -f4 | tr -d '"')
	A=$(echo "$L" | grep "^$G|" | cut -sd'|' -f3 | tr -d '"')
	
	fsthost${A} -l -p -j '' "$P" 1>/tmp/fsthost_menu.log.$G 2>&1 &
}

FSTHOST_DB=${1:-$HOME/.fsthost.xml}
if [ -f "$FSTHOST_DB" ]; then
	MODE='FSTHOST_DB'
elif [ -n "$VST_PATH" ]; then
	MODE='VST_PATH'
	exit 2
else	
	echo "No such file: $FSTHOST_DB"
	echo 'VST_PATH environment is empty'
	exit 1
fi
echo "MODE: $MODE | FSTHOST_DB: $FSTHOST_DB"

L=$(
	T=1
	fsthost_list | while read F; do
		echo "$T|$F"
		T=$((T+1))
	done
)

type zenity 1>/dev/null 2>&1 || ZENITY=0

rm -f /tmp/fsthost_menu.log.*
while true; do main_loop; done
