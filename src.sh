#!/bin/bash

if [ $# -ne 2 ] ;then
	echo "Usage ./src.sh <dir> <ip>"
	exit 1
fi

DIR_TOS=$1
IP_ADDR=$2

set -- $(ls "$DIR_TOS")

if [ $? -ne 0 ] ;then 
	echo "something is wrong.."
	exit 1
fi

# statistics

TOTAL_SENT=0
TOTAL_RECVED=0
DUP_BLKS=0

for file in $@
do
	echo "$file"
#	./rzsrc "$file" "$IP_ADDR"
done

exit 0
