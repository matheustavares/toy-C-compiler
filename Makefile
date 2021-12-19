CC ?= gcc
CFLAGS ?= -Wall -O3 -Wno-unused-function
LDFLAGS ?=

HEADERS = $(wildcard */*.h)
SRCS    = $(wildcard *.c)
OBJS_DIR = objs
OBJS = $(addprefix $(OBJS_DIR)/,$(SRCS:.c=.o))

cc: Makefile $(OBJS) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(OBJS_DIR)/%.o: %.c Makefile $(HEADERS)
	@mkdir -p $(OBJS_DIR)
	$(CC) $(CFLAGS) $< -c -o $@

.PHONY: clean tags test
clean:
	rm -rf cc objs

tags: $(SRCS) $(HEADERS)
	rm -f $@
	ctags -o $@ $(SRCS) $(HEADERS)

test:
	cd compiler-tests && ./test_compiler.sh ../cc $(STAGES)
