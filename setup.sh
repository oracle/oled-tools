#!/bin/bash

kernel_ver=`uname -r`
if echo "$kernel_ver" | grep -q "el8"; then
    find -type f -exec sed -i '1s=^#!/usr/bin/\(python\|env python\)[23]\?=#!/usr/bin/python3=' {} +
    find . -type f -name "Makefile" -print0 | xargs -0 sed -i  's/\bpython\b/python3/g'
    find -type f -exec sed -i 's/\braw_input\b/input/g' {} \;
fi
./configure
