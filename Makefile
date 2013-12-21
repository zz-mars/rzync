CC_FLAGS = -g -o
LINK_LIBS = -levent
BINS = adler32 rzsrc rzdst md_of_file
.PHONY : all
all : adler32

adler32 : adler32_test.o checksum.o
	gcc $? $(CC_FLAGS) $@

.PHONY : cs
cs : rzdst rzsrc
rzsrc : rzsrc.o checksum.o md5.o
	gcc $? $(CC_FLAGS) $@ 
rzdst : rzdst.o checksum.o md5.o
	gcc $? $(CC_FLAGS) $@ $(LINK_LIBS)

md_of_file : md_of_file.o md5.o
	gcc $? $(CC_FLAGS) $@

.PHONY : clean
clean :
	-rm *.o $(BINS)

