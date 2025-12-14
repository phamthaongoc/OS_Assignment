#!/bin/bash

TEST=os_0_mlq_paging
REF=output/${TEST}.output
OUT=my_output.txt

./os $TEST > $OUT

if diff -u $REF $OUT > diff.log; then
    echo "✅ PASS"
else
    echo "❌ FAIL"
    echo "---- FIRST DIFFERENCES ----"
    head -n 40 diff.log
fi
