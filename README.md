rzync
=====

my version of rsync,only for test
=====

This is a very simple and naiive implementation of file synchronization tool.
ONLY FOR TEST AND NO WARRANT AT ALL.

Compile from src
=====

1) git clone https://github.com/zz-mars/rzync.git

2) cd rzync

3) make

Dependency : libevent

Two BINs : rzdst & rzsrc
====

rzdst : The receiver of synchronization which listens on a particular port.
		Simply run it. Currently, no parameter is supported.

rzsrc : The sender of synchronization, run it with one parameter which is the 
		file to be synchronized.

How does it work?
=====

1) The src side sends a sync request which consists of some basic information
   of the file to be sent, e.g. file name,md5 checksum, file size..

2) Once the dst received this request, it looks in its local directory for the
   file with exactly the same name as specified in the sync request. Then, it 
   send the checksums of the blocks of the file to the src side.

3) Src receives these checksums, put them into a hash table for fast-searching the 
   duplicated block.

4) The synchronization data will be sent in the form of delta-encoding, which basically
   means only the data of un-duplicated block will be sent, for those who are duplicates,
   references of the blocks will be sent instead.
   
5) The protocol is based on the ASCII string and very simply.
   Check rzync.h for more information.

THIS PROJECT IS JUST FOR TEST
