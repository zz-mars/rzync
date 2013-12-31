BIN = mktestcase
mktestcase : mktestcase.o
	gcc $? -g -o $@

.PHONY : clean
clean :
	-rm $(BIN) *.o
