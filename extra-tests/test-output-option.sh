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

cat >"$tmpdir"/main.c <<-EOF
int main()
{
	return 2 + 2;
}
EOF

"$test_cc" -o "$tmpdir"/out.o -c "$tmpdir"/main.c
"$test_cc" -o "$tmpdir"/out.s -S "$tmpdir"/main.c
"$test_cc" -o "$tmpdir"/out "$tmpdir"/main.c

(
	cd "$tmpdir"
	file out.o >file-obj
	grep -q relocatable file-obj

	file out.s >file-asm
	grep -q "assembler source" file-asm

	file out >file-bin
	grep -q executable file-bin
)

"$test_cc" -o "$tmpdir"/out1 -o "$tmpdir"/out2 "$tmpdir"/main.c
! test -e "$tmpdir"/out1
test -e "$tmpdir"/out2
