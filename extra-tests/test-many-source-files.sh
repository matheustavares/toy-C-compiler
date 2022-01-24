#!/bin/bash

set -e

test_cc="$1"

if test -z "$test_cc" || test "$1" = "-h" || test "$1" = "--help"
then
	echo "usage: $0 <cc path>"
	exit 1
fi

cleanup() {
	if test -n "$tmpdir"
	then
		rm -rf "$tmpdir"
	fi
}
trap cleanup EXIT

tmpdir="$(mktemp -d tmp-cc-test.XXXXXXXXXX)"

cat >"$tmpdir"/four.c <<-EOF
int two();
int main()
{
	return two() + two();
}
EOF

cat >"$tmpdir"/two.c <<-EOF
int two()
{
	return 2;
}
EOF

test_file() {
	file "$1" > "$1-file"
	grep -q "$2" "$1-file"
	rm "$1-file"
}

test_obj() { test_file "$1" relocatable; }
test_bin() { test_file "$1" executable; }
test_asm() { test_file "$1" "assembler source"; }

clean() {
	find . -type f  ! -name "*.c"  -delete
}

(
	cd "$tmpdir"

	# TEST: many source
	"../$test_cc" -o four four.c two.c
	test_bin four
	clean

	# TEST: many source, no -o
	"../$test_cc" four.c two.c
	test_bin a.out
	clean

	# TEST: many source interleaved args
	"../$test_cc" four.c -o four two.c
	test_bin four
	clean

	# TEST: many source, with -c
	"../$test_cc" -c four.c two.c
	test_obj four.o
	test_obj two.o
	clean

	# TEST: many source, with -S
	"../$test_cc" -S four.c two.c
	test_asm four.s
	test_asm two.s
	clean

	# TEST: -o is not allowed with -c and -S
	! "../$test_cc" -o nope -c four.c two.c 2>err
	grep -Fq "fatal: -S and -c can only be used with -o for a single source file" err
	clean
	! "../$test_cc" -o nope -S four.c two.c 2>err
	grep -Fq "fatal: -S and -c can only be used with -o for a single source file" err
	clean
)
