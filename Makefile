CC_FLAGS = -g -o
LINK_LIBS = -levent
BINS = adler32 src dst md_of_file
.PHONY : all
all : adler32

adler32 : adler32_test.o
	gcc $? $(CC_FLAGS) $@

.PHONY : cs
cs : dst src
src : src.o checksum.o	md5.o
	gcc $? $(CC_FLAGS) $@ 
dst : dst.o checksum.o md5.o
	gcc $? $(CC_FLAGS) $@ $(LINK_LIBS)

md_of_file : md_of_file.o md5.o
	gcc $? $(CC_FLAGS) $@

.PHONY : clean
clean :
	-rm *.o $(BINS)

