#!/bin/sh

DAOS_DIR=${DAOS_DIR:-$(cd $(dirname $0)/..; echo $PWD)}
PCL=$DAOS_DIR/tests/pseudo_cluster

echo $PCL

PAUSE=yes DAOS_DEBUG=0 \
$PCL -C b:4,t:8,p	-P t:r,d:b,n:1,p \
     -C b:4:0,t:8,p	-P t:r,d:b,n:1,p \
     -C b:4,t:16,p	-P t:r,d:b,n:1,p \
     -S s:4,r:3				 \
     -O n:1,p				 \
     -O n:100000			 \
     -T d:30,e:30
