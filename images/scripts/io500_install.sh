#!/bin/bash
prepare_build()
{
  ./prepare.sh
}

mkdir -p $IO500_INSTALL_DIR
cd $IO500_INSTALL_DIR
root=$IO500_INSTALL_DIR

MY_DAOS_INSTALL_PATH=${HOME}/daos/install
MY_MFU_INSTALL_PATH=${root}/mpifileutils/install
MY_IO500_PATH=${root}/io500
git clone https://github.com/IO500/io500.git -b io500-sc20 "${MY_IO500_PATH}"
cd "${MY_IO500_PATH}"

cat << EOF > io500_prepare.patch
diff --git a/prepare.sh b/prepare.sh
index de354ee..a2964d7 100755
--- a/prepare.sh
+++ b/prepare.sh
@@ -7,8 +7,8 @@ echo It will also attempt to build the benchmarks
 echo It will output OK at the end if builds succeed
 echo

-IOR_HASH=bd76b45ef9db
-PFIND_HASH=9d77056adce6
+IOR_HASH=
+PFIND_HASH=mfu_integration

 INSTALL_DIR=\$PWD
 BIN=\$INSTALL_DIR/bin
@@ -59,14 +59,14 @@ function get_ior {

 function get_pfind {
   echo "Preparing parallel find"
-  git_co https://github.com/VI4IO/pfind.git pfind \$PFIND_HASH
+  git_co https://github.com/mchaarawi/pfind pfind \$PFIND_HASH
 }

 ###### BUILD FUNCTIONS
 function build_ior {
   pushd \$BUILD/ior
   ./bootstrap
-  ./configure --prefix=\$INSTALL_DIR
+  ./configure --prefix=\$INSTALL_DIR --with-daos=${MY_DAOS_INSTALL_PATH}
   cd src
   \$MAKE clean
   \$MAKE install
EOF

git apply io500_prepare.patch

cat << EOF > io500_Makefile.patch
diff --git a/Makefile b/Makefile
index 2975471..5dce307 100644
--- a/Makefile
+++ b/Makefile
@@ -1,10 +1,13 @@
 CC = mpicc
 CFLAGS += -std=gnu99 -Wall -Wempty-body -Werror -Wstrict-prototypes -Werror=maybe-uninitialized -Warray-bounds
+CFLAGS += -I${MY_DAOS_INSTALL_PATH}/include -I${MY_MFU_INSTALL_PATH}/include

 IORCFLAGS = \$(shell grep CFLAGS ./build/ior/Makefile | cut -d "=" -f 2-)
 CFLAGS += -g3 -lefence -I./include/ -I./src/ -I./build/pfind/src/ -I./build/ior/src/
 IORLIBS = \$(shell grep LIBS ./build/ior/Makefile | cut -d "=" -f 2-)
 LDFLAGS += -lm \$(IORCFLAGS) \$(IORLIBS) # -lgpfs # may need some additional flags as provided to IOR
+LDFLAGS += -L${MY_DAOS_INSTALL_PATH}/lib64 -ldaos -ldaos_common -ldfs -lgurt -luuid
+LDFLAGS += -L${MY_MFU_INSTALL_PATH}/lib64 -lmfu_dfind -lmfu

 VERSION_GIT=\$(shell git describe --always --abbrev=12)
 VERSION_TREE=\$(shell git diff src | wc -l | sed -e 's/   *//g' -e 's/^0//' | sed "s/\([0-9]\)/-\1/")
EOF

git apply io500_Makefile.patch

cat << 'EOF' > io500_stonewall.patch
diff --git a/src/phase_find.c b/src/phase_find.c
index e282b25..f2bb69c 100644
--- a/src/phase_find.c
+++ b/src/phase_find.c
@@ -61,6 +61,7 @@ static double run(void){
     int rank;
     MPI_Comm_rank(of.pfind_com, & rank);

+    of.pfind_o->stonewall = 300;
     // pfind supports stonewalling timer -s, but ignore for now
     pfind_find_results_t * res = pfind_find(of.pfind_o);
     if(! res){
EOF

git apply io500_stonewall.patch
export I_MPI_OFI_LIBRARY_INTERNAL=0
export I_MPI_OFI_PROVIDER="tcp;ofi_rxm"
source /opt/intel/oneapi/setvars.sh
# This is expected to error.  After, compile.sh is edited with the correct paths
# and the build can be run again.
prepare_build || true

sed -i "/^DAOS=/c\DAOS=${MY_DAOS_INSTALL_PATH}" ${MY_IO500_PATH}/build/pfind/compile.sh
sed -i "/^MFU=/c\MFU=${MY_MFU_INSTALL_PATH}" ${MY_IO500_PATH}/build/pfind/compile.sh
prepare_build || true

cd build/ior
git checkout a90d414a304b53c64d331d09104cc8df8bda0226
make install
cd ../../
make clean
make

wget https://raw.githubusercontent.com/mchaarawi/io500/main/config-full.ini
sed -i 's/ --dfs.svcl=$DAOS_SVCL//g' config-full.ini

echo "Complete!"
