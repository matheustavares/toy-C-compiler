#!/bin/bash


tmpdir="$(mktemp -d test-tmp.XXXXXXXXXX)"
cleanup () {
	rm -rf "$tmpdir"
}
trap cleanup EXIT

test -x ./test-tempfile || {
	echo "./test-tempfile is missing or not executable"
	exit 1
}

cat >$tmpdir/expect <<-EOF &&
create-template '$tmpdir/tmp-XXXXXX'
commit
EOF

echo "TEST: creation template" &&
./test-tempfile create-template=$tmpdir/tmp-XXXXXX commit >$tmpdir/actual &&
diff -u $tmpdir/expect $tmpdir/actual &&
test -a $tmpdir/tmp-* &&
rm $tmpdir/tmp-* &&


cat >$tmpdir/expect <<-EOF &&
create-path '$tmpdir/tmp'
commit
EOF

echo "TEST: creation path" &&
./test-tempfile create-path=$tmpdir/tmp commit >$tmpdir/actual &&
diff -u $tmpdir/expect $tmpdir/actual &&
test -a $tmpdir/tmp &&
rm $tmpdir/tmp &&

cat >$tmpdir/expect <<-EOF &&
create-path '$tmpdir/tmp'
exit
EOF

echo "TEST: auto remove on exit" &&
./test-tempfile create-path=$tmpdir/tmp exit >$tmpdir/actual &&
diff -u $tmpdir/expect $tmpdir/actual &&
! test -a $tmpdir/tmp &&

cat >$tmpdir/expect <<-EOF &&
create-path '$tmpdir/tmp'
signal
EOF

echo "TEST: auto remove on signal" &&
./test-tempfile create-path=$tmpdir/tmp signal >$tmpdir/actual
diff -u $tmpdir/expect $tmpdir/actual &&
! test -a $tmpdir/tmp &&

cat >$tmpdir/expect <<-EOF &&
create-path '$tmpdir/tmp'
remove
EOF

echo "TEST: manual remove" &&
./test-tempfile create-path=$tmpdir/tmp remove >$tmpdir/actual &&
diff -u $tmpdir/expect $tmpdir/actual &&
! test -a $tmpdir/tmp &&

cat >$tmpdir/expect <<-EOF &&
create-path '$tmpdir/tmp'
rename '$tmpdir/other'
EOF

echo "TEST: manual rename" &&
./test-tempfile create-path=$tmpdir/tmp rename=$tmpdir/other >$tmpdir/actual &&
diff -u $tmpdir/expect $tmpdir/actual &&
! test -a $tmpdir/tmp &&
test -a $tmpdir/other &&

cat >$tmpdir/expect <<-EOF &&
create-path '$tmpdir/tmp'
write-fd 'str1'
write-fd 'str2'
commit
EOF

cat >$tmpdir/expect-write <<-EOF &&
str1
str2
EOF

echo "TEST: write fd" &&
./test-tempfile create-path=$tmpdir/tmp write-fd=str1 write-fd=str2 commit >$tmpdir/actual &&
diff -u $tmpdir/expect $tmpdir/actual &&
diff -u $tmpdir/expect-write $tmpdir/tmp &&
rm $tmpdir/tmp &&

cat >$tmpdir/expect <<-EOF &&
create-path '$tmpdir/tmp'
write-fp 'str1'
write-fp 'str2'
commit
EOF

cat >$tmpdir/expect-write <<-EOF &&
str1
str2
EOF

echo "TEST: write fp" &&
./test-tempfile create-path=$tmpdir/tmp write-fp=str1 write-fp=str2 commit >$tmpdir/actual &&
diff -u $tmpdir/expect $tmpdir/actual &&
diff -u $tmpdir/expect-write $tmpdir/tmp &&
rm $tmpdir/tmp &&


cat >$tmpdir/expect <<-EOF &&
create-path '$tmpdir/tmp'
create-path '$tmpdir/tmp2'
switch-pointer
create-path '$tmpdir/tmp3'
create-path '$tmpdir/tmp4'
switch-pointer
commit
create-path '$tmpdir/tmp5'
EOF

echo "TEST: many create and commit one in the middle" &&
./test-tempfile create-path=$tmpdir/tmp create-path=$tmpdir/tmp2 switch-pointer create-path=$tmpdir/tmp3 create-path=$tmpdir/tmp4 switch-pointer commit create-path=$tmpdir/tmp5 >$tmpdir/actual &&

diff -u $tmpdir/expect $tmpdir/actual &&
test -a $tmpdir/tmp2 &&
rm $tmpdir/tmp2 &&
! test -a $tmpdir/tmp* &&

echo "OK"
