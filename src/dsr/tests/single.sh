#!/bin/sh

DAOS_DIR=${DAOS_DIR:-$(cd $(dirname $0)/..; echo $PWD)}
PCL=$DAOS_DIR/tests/pseudo_cluster

echo $PCL

# -C R:2,b:8,t:16,p	: Create a cluster map with total 2 racks (R:2),
#			  8 boards (b:8), 16 targets (t:16), print it (p)
# -P t:r,d:b,n:1,p	: Create a rim map (t:r), domain is board (d:b), it
#			  has a single rim (n:1), print it (p).
# -S s:2,r:3		: Set object schema, stripe count is 2 (s:2), 3-way
#			  replication (r:3)
# -O n:1,p		: Create one object (n:1), print its layout (p)
# -O n:1000000		: Create one million objects (n:1000000)
# -T p			: Print object distribution in all targets (p)
# -T d:13,e:13		: Disable target 13 (d:13), then enable it (e:13)
# -C b:8:0,t:16,p	: Change cluster map by adding 16 targets (t:16) to
#			  8 boards which start from 0 (b:8:0), print the
#			  updated cluster map (p)
# -T p			: Print object distribution in all targets (p)
# -R			: Rebalance objects
# -C r:2:0,b:8,t:32,p	: Change cluster map by adding 32 targets (t:32) to
#			  8 new boards (b:8), these boards are evenly
#			  distributed under 2 racks which start from 0 (r:2:0),
#			  print the updated cluster map (p)
# -T p			: Print object distribution in all targets (p)
# -R			: Rebalance objects

SKIP=0

PAUSE=yes DAOS_DEBUG=0		\
$PCL	-C R:2,b:8,t:16,p	\
	-P t:r,d:b,n:1,p	\
	-S s:2,r:3,k:$SKIP	\
	-O n:1,p		\
	-O n:1000000		\
	-T p			\
	-T d:13,e:13		\
	-C b:8:0,t:16,p		\
	-T p			\
	-R			\
	-C r:2:0,b:8,t:32,p	\
	-T p			\
	-R
