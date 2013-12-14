CC_FLAGS = -g -o
BINS = adler32 
.PHONY : all
all : adler32 

adler32 : adler32_test.o
	gcc $? $(CC_FLAGS) $@

.PHONY : clean
clean :
	-rm *.o $(BINS)

