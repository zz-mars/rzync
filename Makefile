CC_FLAGS = -g -o
LINK_LIBS = -levent
BINS = rzsrc rzdst cdc
.PHONY : all
all : rzdst rzsrc
rzsrc : rzsrc.o adler32.o md5.o
	gcc $? $(CC_FLAGS) $@ 
rzdst : rzdst.o adler32.o md5.o
	gcc $? $(CC_FLAGS) $@ $(LINK_LIBS)
cdc : cdc.o md5.o adler32.o
	gcc $? $(CC_FLAGS) $@

.PHONY : clean
clean :
	-rm *.o $(BINS)

