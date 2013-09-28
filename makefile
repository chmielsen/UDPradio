TARGETS = nadajnik odbiornik

CC     = gcc
CFLAGS =  -Wall -pthread
LFLAGS =  -Wall -pthread 

all: $(TARGETS)


nadajnik: nadajnik.o common.h err.o err.h queue.h archiver.h
	$(CC) $(LFLAGS) $^ -o $@

odbiornik: odbiornik.o common.h err.o err.h
	$(CC) $(LFLAGS) $^ -lm /usr/include/math.h -o $@


.PHONY: clean TARGET

clean:
	rm -f $(TARGETS) *.o *~ *.bak
