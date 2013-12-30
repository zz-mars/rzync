#!/bin/bash

if [ $# -ne 2 ] ;then
	echo "Usage ./src.sh <dir> <ip>"
fi

DIR_TOS=$1
IP_ADDR=$2

set -- $(ls "$DIR_TOS")

if [ $? -ne 0 ] ;then 
	echo "something is wrong.."
fi

for file in $@
do
	echo "$file"
done

