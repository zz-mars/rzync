CC_FLAGS = -g -o
BINS = adler32 rolling
.PHONY : all
all : adler32 rolling

adler32 : adler32.o
	gcc $? $(CC_FLAGS) $@

rolling : rolling.o
	gcc $? $(CC_FLAGS) $@ 

.PHONY : clean
clean :
	-rm *.o $(BINS)

