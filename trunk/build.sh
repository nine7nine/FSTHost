#!/bin/sh

PLAT=$(getconf LONG_BIT)
if [[ $PLAT -eq 64 ]]; then
	make fsthost64
fi

make fsthost_list

# Always build 32bit version
export PKG_CONFIG_PATH=/usr/lib32/pkgconfig
make fsthost32
