VPATH = /usr/include/aosen/
OBJECT = main.o

CC = gcc -Wall
LIB_OBJECT = libaosen.so
SHARED = -shared -o 
APPNAME = aosen

main: $(OBJECT)
	$(CC) -o $(APPNAME) $(OBJECT) -laosen -lm

main.o: aosen_core.h aosen_cJSON.h

.PHONY : clean
clean:
	@echo "cleanning project"
	-rm $(APPNAME) *.o
	@echo "clean completed"
