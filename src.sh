#!/bin/bash

if [ $# -ne 2 ] ;then
	echo "Usage ./src.sh <ip> <dir>"
	exit 1
fi

IP_ADDR=$1
DIR_TOS=$2

echo "IP_ADDR : $IP_ADDR"
echo "DIR_TOS : $DIR_TOS"

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
#	./rzsrc "$IP_ADDR" "$DIR_TOS" "$file" 
done

exit 0
