CC_FLAGS = -g -o
LINK_LIBS = -levent
BINS = adler32 src dst
.PHONY : all
all : adler32

adler32 : adler32_test.o
	gcc $? $(CC_FLAGS) $@

.PHONY : cs
cs : dst src
src : src.o
	gcc $? $(CC_FLAGS) $@ 
dst : dst.o checksum.o md5.o
	gcc $? $(CC_FLAGS) $@ $(LINK_LIBS)

.PHONY : clean
clean :
	-rm *.o $(BINS)

