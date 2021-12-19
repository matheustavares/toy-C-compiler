#!/bin/bash

for f in test-*.sh
do
	./$f || {
		echo "TEST FAILED"
		exit 1
	}
done
