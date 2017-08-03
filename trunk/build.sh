#!/bin/sh

PLAT=$(getconf LONG_BIT)
if [[ $PLAT -eq 64 ]]; then
	export PKG_CONFIG_PATH=/usr/lib/pkgconfig
	make fsthost64
fi

# Always build 32bit version
export PKG_CONFIG_PATH=/usr/lib32/pkgconfig
make fsthost32

make fsthost_list
