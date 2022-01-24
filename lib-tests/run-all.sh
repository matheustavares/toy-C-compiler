#!/bin/bash

for f in test-*.sh
do
	echo "TEST ====== '$f'"
	./$f || {
		echo "TEST FAILED"
		exit 1
	}
done
