CC_FLAGS = -g -o
BINS = adler32 s c
.PHONY : all
all : adler32

adler32 : adler32_test.o
	gcc $? $(CC_FLAGS) $@

.PHONY : cs
cs : c s
c : c.o
	gcc $? $(CC_FLAGS) $@ 
s : s.o
	gcc $? $(CC_FLAGS) $@ -levent

.PHONY : clean
clean :
	-rm *.o $(BINS)

