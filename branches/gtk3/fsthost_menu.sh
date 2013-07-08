#!/bin/sh

# FSTHost menu in dialog
#   by Xj <xj@wp.pl>

PATH=.:$PATH

fsthost='fsthost32'
ZENITY=1
ZENITY_WIDTH=300
ZENITY_HEIGHT=700
MODE='' # Will be set later

list_fst() {
	if [ $MODE = 'VST_PATH' ]; then
		# Get from VST_PATH
		find $(echo $VST_PATH|tr ':' ' ') -iname '*.dll' -type f | awk -F'/' '{print "\""$NF"\"|\""$0"\""}'
	else
		fsthost_list
	fi
}

show_menu() {
	echo "$L" | cut -d'|' -f1-2 |
	{
	if [ $ZENITY -eq 1 ]; then
		tr '|' '\n' |
		zenity --title 'Wybierz Host' \
			--width=$ZENITY_WIDTH \
			--height=$ZENITY_HEIGHT \
			--list \
			--column 'Number' \
			--column 'Plugin'
	else
		tr '|' ' ' | xargs dialog --menu 'Select VST' 0 0 0 3>&1 1>&2 2>&3
	fi
	}
}

main_loop() {
	G=$(show_menu)
	[ -z "$G" ] && exit
	
	F=$(echo "$L" | grep "^$G|" | cut -d'|' -f3 | tr -d '"')
	
	$fsthost -l -p -j '' "$F" 1>/tmp/fsthost_menu.log.$G 2>&1 &
}

FSTHOST_DB=${1:-$HOME/.${fsthost}.xml}
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
	list_fst | while read F; do
		echo "$T|$F"
		T=$((T+1))
	done
)

type zenity 1>/dev/null 2>&1 || ZENITY=0

rm -f /tmp/fsthost_menu.log.*
while true; do main_loop; done
