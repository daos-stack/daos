#!/bin/bash

ret=1

while [ $ret -ne 0 ]; do
        ./jenkins_time.py
        ret=$?
done
