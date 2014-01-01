#!/bin/bash

if [ $# -ne 1 ] ;then
	echo "Usage: rm_space_in_fn.sh <dir>"
	exit 1
fi

DIRZ=$1

cd $DIRZ

if [ $? -ne 0 ] ;then
	exit 1
fi

ls | while read file;
do
	mv "$file" $(echo $file | tr -d ' ') 2>/dev/null
done

cd - > /dev/null

exit 0

