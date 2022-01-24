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

(
	cd "$tmpdir"
	touch x.c

	# Should remove tempfiles on assembler/linkage error
	! "../$test_cc" x.c 2>/dev/null
	rm x.c
	test -z "$(ls -A .)"

	echo 'int main() { return 0; }' >main.c
	"../$test_cc" -S main.c
	# Should not remove main.s from step above
	"../$test_cc" -o main main.c
	test -a main
	test -a main.s
)
