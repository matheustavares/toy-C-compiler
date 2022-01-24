CC ?= gcc
CFLAGS := -Wall -O3 -Wno-unused-function $(CFLAGS)
LDFLAGS ?=

MAIN = cc
HEADERS = $(wildcard *.h lib/*.h)
SRCS = $(wildcard *.c lib/*.c)
STAGES ?= 1 2 3 4 5 6 7 8 \
	  4-more-binary-ops 5-compound-assignment goto complex_identifiers \
	  comments

OBJS_DIR = objs
OBJS = $(addprefix $(OBJS_DIR)/,$(filter-out $(MAIN).o,$(SRCS:.c=.o)))

$(MAIN): $(OBJS_DIR)/$(MAIN).o Makefile $(OBJS) $(HEADERS) .MAKE-LDFLAGS
	$(CC) $(CFLAGS) $(OBJS) $< -o $@

$(OBJS_DIR)/%.o: %.c Makefile $(HEADERS) .MAKE-CFLAGS
	@mkdir -p $(OBJS_DIR)/lib
	$(CC) $(CFLAGS) $< -c -o $@

.PHONY: debug
debug:
	@CFLAGS="-O0 -g -fno-omit-frame-pointer" $(MAKE)

###############################################################################
# Testing
###############################################################################

LIB_TESTS_SRCS = $(wildcard lib-tests/test-*.c)
LIB_TESTS_BINS = $(basename $(LIB_TESTS_SRCS))

$(LIB_TESTS_BINS): %: %.c Makefile $(OBJS) $(HEADERS) .MAKE-LDFLAGS
	$(CC) $(CFLAGS) $< $(OBJS) -o $@

.PHONY: tests lib-tests compiler-tests extra-tests
tests: lib-tests compiler-tests extra-tests
lib-tests: $(LIB_TESTS_BINS)
	cd lib-tests && ./run-all.sh
compiler-tests: $(MAIN)
	cd compiler-tests && ./test_compiler.sh ../$(MAIN) $(STAGES)
extra-tests: $(MAIN)
	@cd extra-tests && \
	echo "========= extra tests:" && \
	for testfile in `ls test-*.sh`; do \
		./$$testfile ../$(MAIN); \
		if test $$? -eq 0; then \
			echo "PASSED: $$testfile"; \
		else \
			echo "FAILED: $$testfile"; \
		fi \
	done

###############################################################################
# Misc rules
###############################################################################

.PHONY: FORCE

# The technique (and code) used here to trigger a new build on change of
# CFLAGS and/or LDFLAGS as appropriated comes from Git. See its Makefile, lines
# 2768 to 2782 at commit 88d915a634 ("A few fixes before -rc2", 2021-11-04)
# for the original source.

.MAKE-CFLAGS: FORCE
	@if ! test -e .MAKE-CFLAGS && test x"$(CFLAGS)" = x; then \
		touch .MAKE-CFLAGS; \
	elif test x"$(CFLAGS)" != x"`cat .MAKE-CFLAGS 2>/dev/null`"; then \
		echo >&2 "    * new build flags"; \
		echo "$(CFLAGS)" >.MAKE-CFLAGS; \
        fi

.MAKE-LDFLAGS: FORCE
	@if ! test -e .MAKE-LDFLAGS && test x"$(LDFLAGS)" = x; then \
		touch .MAKE-LDFLAGS; \
	elif test x"$(LDFLAGS)" != x"`cat .MAKE-LDFLAGS 2>/dev/null`"; then \
		echo >&2 "    * new link flags"; \
		echo "$(LDFLAGS)" >.MAKE-LDFLAGS; \
        fi

.PHONY: clean tags
clean:
	rm -rf $(MAIN) objs $(LIB_TESTS_BINS)

tags: $(SRCS) $(HEADERS)
	rm -f $@
	ctags -o $@ $(SRCS) $(HEADERS) $(LIB_TESTS_SRCS)

