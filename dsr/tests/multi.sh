#!/bin/sh
DAOS_DIR=${DAOS_DIR:-$(cd $(dirname $0)/..; echo $PWD)}
PCL=$DAOS_DIR/tests/pseudo_cluster

NOBJS_DEF=`expr 10000 \* 1000`

NOBJS=${NOBJS:-$NOBJS_DEF}
TG_PER_BOARD=${TG_PER_BOARD:-4}

TARGET=1
#for NB_K in 256 128 64 32 16 8 4 2 1 ; do
for NB_K in 1 2 4 ; do
	for SKIP in 0 ; do
		NBOARDS=`expr $NB_K \* 1024`
		NTARGETS=`expr $NBOARDS \* $TG_PER_BOARD`

		for NRIMS in 1 2 4 8 ; do
			echo "NBOARDS=$NBOARDS, NTARGETS=$NTARGETS "\
			     "NOBJS=$NOBJS, NRIMS=$NRIMS, SKIP=$SKIP"
			echo "--------------------------------------------"

			$PCL -C b:$NBOARDS,t:$NTARGETS	\
			     -P t:r,d:b,n:$NRIMS	\
			     -S s:4,r:3,k:$SKIP		\
			     -O n:$NOBJS		\
			     -T d:$TARGET,e:$TARGET
			echo "DONE"
			echo ""
			sleep 5
		done
	done
done
