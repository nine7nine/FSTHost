#!/bin/bash

repair_links() {
	cd $1 || return

	find -L . -type l -delete
	find . -name '*.so.*' -type f |
		while read L; do
			G=${L%%.so.*}.so;
			ln -s $L $G;
		done
}

repair_links /usr/lib/i386-linux-gnu
repair_links /lib/i386-linux-gnu
