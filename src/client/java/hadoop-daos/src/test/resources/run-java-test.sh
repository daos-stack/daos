#!/usr/bin/sh

#-Dthreads for running test concurrently in single JVM
#-Djvms for running test in multiple JVMs

pdsh -R ssh -w gsr13[5-6] "/root/daos/java-test.sh read " \
"-Dpid=0642c096-babe-40d9-a61c-25f99d9ae803 " \
"-Duid=51fcf0ad-c9ec-47b7-8b20-963b753c2cbc " \
"-Djvms=15 " \
"-DfileSize=1073741824"
