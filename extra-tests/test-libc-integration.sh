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

cat >"$tmpdir/fib.c" <<-EOF
int getchar(void);
int putchar(int c);

int fibbonacci(int n)
{
	if (n == 1)
		return 0;
	if (n <= 3)
		return 1;
	/* Very inefficient. Only to test recursion. */
	return fibbonacci(n-1) + fibbonacci(n-2);
}

int getint(void)
{
	int val = 0;
	int got_input = 0;
	/* We don't have 'EOF' define, so stop on any negative number. */
	for (int c = getchar(); c >= 0; c = getchar()) {
		if (c == 10) // newline
			break;
		if (c < 48 || c > 57) // Error: not a digit
			return -1;
		c -= 48;
		val *= 10;
		val += c;
		got_input = 1;
	}
	return got_input ? val : -1;
}

void putint(int val)
{
	if (val < 0)
		return;

	int divisor = 1;
	for (int val_cpy = val; val_cpy / 10; val_cpy /= 10)
		divisor *= 10;

	while (divisor) {
		int digit = val / divisor;
		putchar(digit + 48);
		val -= digit * divisor;
		divisor /= 10;
	}
	putchar(10);
}

int main()
{
	int val = getint();
	if (val <= 0)
		return 1;
	for (int i = 1; i <= val; i++)
		putint(fibbonacci(i)); // Again: inefficient, only for simplicity 
	return 0;
}
EOF

cat >"$tmpdir/expect" <<-EOF
0
1
1
2
3
5
8
13
21
34
55
89
144
233
EOF

"$test_cc" -o "$tmpdir/fib" "$tmpdir/fib.c"
echo 14 | "$tmpdir/fib" >"$tmpdir/actual"
diff -u "$tmpdir/expect" "$tmpdir/actual"
