/*
 * Saturation Test
 * Written by Xiaodong Liu <xiaodong.liu@intel.com>
 */

This tool is used to judge the saturation performance of ISA-L's multi-buffer hash and other algorithms.
It can be used to give a comparision between multi-buffer hash and OpenSSL's single buffer hash.

Compilation:
(Make sure isa-l_crypto library is already installed. Other libs requried are openssl and pthread.)
make

Usage: ./isal_multithread_perf -n num_threads
        -v verbose output
        -t time to run(secs)
        -n number of algorithm threads
        -l len of each buffer(KB)
        -a memory copy before algorithm -- 1 do(default); 0 not do
        -b memory copy after algorithm -- 1 do(default); 0 not do
        -m method of algorithm:  md5  md5_mb  sha1  sha1_mb  sha256  sha256_mb
          sha512  sha512_mb  cbc_128_dec  cbc_192_dec  cbc_256_dec  xts_128_enc
          xts_256_enc  gcm_128_enc  gcm_256_enc

Example:
./isal_multithread_perf -m md5 -n 10
