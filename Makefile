CC ?= gcc
CFLAGS ?= -Wall -O3 -Wno-unused-function
LDFLAGS ?=

MAIN = cc
HEADERS = $(wildcard *.h lib/*.h)
SRCS = $(wildcard *.c lib/*.c)

OBJS_DIR = objs
OBJS = $(addprefix $(OBJS_DIR)/,$(filter-out $(MAIN).o,$(SRCS:.c=.o)))

$(MAIN): $(OBJS_DIR)/$(MAIN).o Makefile $(OBJS) $(HEADERS)
	$(CC) $(CFLAGS) $(OBJS) $< -o $@

$(OBJS_DIR)/%.o: %.c Makefile $(HEADERS)
	@mkdir -p $(OBJS_DIR)
	$(CC) $(CFLAGS) $< -c -o $@

###############################################################################
# Testing
###############################################################################

LIB_TESTS_SRCS = $(wildcard lib-tests/test-*.c)
LIB_TESTS_BINS = $(basename $(LIB_TESTS_SRCS))

$(LIB_TESTS_BINS): %: %.c Makefile $(OBJS) $(HEADERS)
	$(CC) $(CFLAGS) $< $(OBJS) -o $@

.PHONY: tests lib-tests compiler-tests
tests: lib-tests compiler-tests
lib-tests: $(LIB_TESTS_BINS)
	cd lib-tests && ./run-all.sh
compiler-tests:
	cd compiler-tests && ./test_compiler.sh ../$(MAIN) $(STAGES)

###############################################################################
# Misc rules
###############################################################################

.PHONY: clean tags
clean:
	rm -rf $(MAIN) objs $(LIB_TESTS_BINS)

tags: $(SRCS) $(HEADERS)
	rm -f $@
	ctags -o $@ $(SRCS) $(HEADERS) $(LIB_TESTS_SRCS)

