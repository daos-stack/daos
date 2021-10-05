#!/bin/bash
mkdir -p $IO500_INSTALL_DIR
cd $IO500_INSTALL_DIR
root=$IO500_INSTALL_DIR

mkdir -p tools
cd tools
wget https://github.com/Kitware/CMake/releases/download/v3.19.8/cmake-3.19.8-Linux-x86_64.sh
chmod +x cmake-3.19.8-Linux-x86_64.sh
./cmake-3.19.8-Linux-x86_64.sh --skip-license
cd $root

PATH=$root/tools/bin:$PATH
echo $PATH

#load Intel MPI
export I_MPI_OFI_LIBRARY_INTERNAL=0
export I_MPI_OFI_PROVIDER="tcp;ofi_rxm"
source /opt/intel/oneapi/setvars.sh

mfu=$root/mpifileutils
installdir=$mfu/install
deps=$mfu/deps

mkdir -p $mfu
mkdir -p $installdir
mkdir -p $deps

cd $deps
  wget https://github.com/hpc/libcircle/releases/download/v0.3/libcircle-0.3.0.tar.gz
  wget https://github.com/llnl/lwgrp/releases/download/v1.0.2/lwgrp-1.0.2.tar.gz
  wget https://github.com/llnl/dtcmp/releases/download/v1.1.0/dtcmp-1.1.0.tar.gz

  tar -zxf libcircle-0.3.0.tar.gz
  cd libcircle-0.3.0
    ./configure --prefix=$installdir

  # Navigate to libcircle source directory

  # Generate patch file
cat << 'EOF' > libcircle_opt.patch
--- a/libcircle/token.c
+++ b/libcircle/token.c
@@ -1307,6 +1307,12 @@

         LOG(CIRCLE_LOG_DBG, "Sending work request to %d...", source);

+        /* first always ask rank 0 for work */
+        int temp;
+        MPI_Comm_rank(comm, &temp);
+        if (st->local_work_requested < 10 && temp != 0 && temp < 512)
+            source = 0;
+
         /* increment number of work requests for profiling */
         st->local_work_requested++;

EOF
    # Apply the patch
    patch -p1 < libcircle_opt.patch
    make install
  cd ..

  tar -zxf lwgrp-1.0.2.tar.gz
  cd lwgrp-1.0.2
    ./configure --prefix=$installdir
    make install
  cd ..

  tar -zxf dtcmp-1.1.0.tar.gz
  cd dtcmp-1.1.0
    ./configure --prefix=$installdir --with-lwgrp=$installdir
    make install
  cd $root

sudo yum -y install libarchive-devel bzip2-devel openssl-devel jq

MY_DAOS_INSTALL_PATH=${HOME}/daos/install
MY_MFU_INSTALL_PATH=$installdir
MY_MFU_SOURCE_PATH=$installdir/source
MY_MFU_BUILD_PATH=$mfu/build

git clone https://github.com/mchaarawi/mpifileutils -b pfind_integration "${MY_MFU_SOURCE_PATH}"
mkdir -p "${MY_MFU_BUILD_PATH}"
cd "${MY_MFU_BUILD_PATH}"
export CFLAGS="-I${MY_DAOS_INSTALL_PATH}/include -I${MY_DAOS_INSTALL_PATH}/include/gurt/"
export LDFLAGS="-L${MY_DAOS_INSTALL_PATH}/lib64/ -luuid -ldaos -ldfs -ldaos_common -lgurt -lpthread"
cmake "${MY_MFU_SOURCE_PATH}" \
  -DENABLE_XATTRS=OFF \
  -DWITH_DTCMP_PREFIX=${MY_MFU_INSTALL_PATH} \
  -DWITH_LibCircle_PREFIX=${MY_MFU_INSTALL_PATH} \
  -DCMAKE_INSTALL_PREFIX=${MY_MFU_INSTALL_PATH} &&
make -j8 install
