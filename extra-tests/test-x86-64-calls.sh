#!/bin/bash

# Check that we implemented the correct calling conventions by compiling
# one function with our cc, another with gcc, and linking them.

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


cat >"$tmpdir"/sum.c <<-EOF
int op(int v1, int v2, int v3, int v4, int v5, int v6, int v7, int v8)
{
	return (v2 + v4 + v6 + v8) - (v1 + v3 + v5 + v7);
}
EOF

cat >"$tmpdir"/main.c <<-EOF
int op(int v1, int v2, int v3, int v4, int v5, int v6, int v7, int v8);
int main()
{
	return op(1, 2, 3, 4, 5, 6, 7, 8);
}
EOF

gcc -c -o "$tmpdir"/gcc-main.o "$tmpdir"/main.c
gcc -c -o "$tmpdir"/gcc-sum.o "$tmpdir"/sum.c
"$test_cc" -c -o "$tmpdir"/test_cc-main.o "$tmpdir"/main.c
"$test_cc" -c -o "$tmpdir"/test_cc-sum.o "$tmpdir"/sum.c

(
	cd "$tmpdir"

	gcc -o reference gcc-main.o     gcc-sum.o
	gcc -o test1     gcc-main.o     test_cc-sum.o
	gcc -o test2     test_cc-main.o gcc-sum.o

	(set +e; ./reference; echo $?) >reference-outcode
	(set +e; ./test1; echo $?) >test1-outcode
	(set +e; ./test2; echo $?) >test2-outcode

	diff reference-outcode test1-outcode
	diff reference-outcode test2-outcode
)
