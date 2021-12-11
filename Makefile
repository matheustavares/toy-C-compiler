CC ?= gcc
CFLAGS ?= -Wall -O3 -Wno-unused-function
LDFLAGS ?=

HEADERS = $(wildcard */*.h)
SRCS    = $(wildcard *.c)
OBJS    = $(SRCS:.c=.o)

cc: Makefile $(OBJS) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c Makefile $(HEADERS)
	$(CC) $(CFLAGS) $< -c -o $@

.PHONY: clean tags
clean:
	rm -f cc *.o

tags: $(SRCS) $(HEADERS)
	rm -f $@
	ctags -o $@ $(SRCS) $(HEADERS)
