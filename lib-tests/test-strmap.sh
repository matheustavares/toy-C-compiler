#!/bin/bash


tmpdir="$(mktemp -d test-tmp.XXXXXXXXXX)"
cleanup () {
	rm -rf "$tmpdir"
}
trap cleanup EXIT

test -x ./test-strmap || {
	echo "./test-strmap is missing or not executable"
	exit 1
}

test_grep () {
	if grep -q "$1" "$2"
	then
		return 0
	else
		echo "'$1' not found in '$2':"
		echo ========
		cat "$2"
		echo ========
		return 1
	fi
}

cat >$tmpdir/expect <<-EOF &&
init 2
has 'a': 0
put: 'a' -> 2
has 'a': 1
find 'a': 2
list
 a -> 2
put: 'a' -> 3
find 'a': 3
list
 a -> 3
info:
  nr:          1
  table_alloc: 2
  keys_alloc:  24
put: 'b' -> 3
info:
  nr:          2
  table_alloc: 2
  keys_alloc:  24
put: 'c' -> 4
info:
  nr:          3
  table_alloc: 4
  keys_alloc:  24
list
 a -> 3
 b -> 3
 c -> 4
copy
list
 a -> 3
 b -> 3
 c -> 4
info:
  nr:          3
  table_alloc: 4
  keys_alloc:  24
copy
list
 a -> 3
 b -> 3
 c -> 4
info:
  nr:          3
  table_alloc: 4
  keys_alloc:  24
destroy
EOF

echo "TEST: many operations" &&
./test-strmap init=2 has=a put=a,2 has=a find=a list put=a,3 find=a list info put=b,3 info put=c,4 info list copy list info copy list info destroy >$tmpdir/actual
if test $? != 0
then
	cat $tmpdir/actual
	exit 1
fi

diff -u $tmpdir/expect $tmpdir/actual &&
echo "OK" &&

echo "TEST: invalid uses" &&
for opt in has=a put=a,2 find=a list destroy "init init" copy
do
	./test-strmap $opt >$tmpdir/actual 2>&1
	{
		test $? != 0 &&
		test_grep BUG $tmpdir/actual
	} || exit 1
done &&
echo "OK" &&

cat >$tmpdir/expect <<-EOF &&
init
put: 'break' -> 2
put: 'unreachable' -> 2
has 'break': 1
has 'unreachable': 1
list
 break -> 2
destroy
EOF

echo "TEST: iterator break" &&
./test-strmap init put=break,2 put=unreachable,2 has=break has=unreachable list destroy >$tmpdir/actual
if test $? != 0
then
	cat $tmpdir/actual
	exit 1
fi

diff -u $tmpdir/expect $tmpdir/actual &&
echo "OK"

cat >$tmpdir/expect <<-EOF &&
init
has 'k': 0
find 'k': not-found
put: 'k' -> 0
has 'k': 1
find 'k': 0
list
 k -> 0
destroy
EOF

echo "TEST: storing NULL" &&
./test-strmap init has=k find=k put=k,0 has=k find=k list destroy >$tmpdir/actual
if test $? != 0
then
	cat $tmpdir/actual
	exit 1
fi

diff -u $tmpdir/expect $tmpdir/actual &&
echo "OK"
