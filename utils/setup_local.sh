
# setup some useful things
echo $PWD
if [ -d "utils" ]; then
  VARS_FILE=./.build_vars-`uname -s`.sh
else
  VARS_FILE=../.build_vars-`uname -s`.sh
fi

if [ ! -f $VARS_FILE ]
then
    echo Build vars file $VARS_FILE does not exist
    echo Cannot continue
    exit 1
fi

. $VARS_FILE

os=`uname`
if [ "$os" = "Darwin" ]; then
    if [ -n "$DYLD_LIBRARY_PATH" ]; then
	export DYLD_LIBRARY_PATH=${SL_LD_LIBRARY_PATH}:$DYLD_LIBRARY_PATH
    else
	export DYLD_LIBRARY_PATH=${SL_LD_LIBRARY_PATH}
    fi
fi

if [ -z "$SL_PREFIX" ]
then
    SL_PREFIX=`pwd`/install
fi

export PATH=$SL_PREFIX/bin:${SL_OMPI_PREFIX}/bin:$PATH
