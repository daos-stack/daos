package com.intel.daos.hadoop.fs;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileSystem;

/**
 *
 */
public class DaosFSFactory {
  private static FileSystem fs = null;
  private final static String pooluuid = "ec94dae2-027b-460a-bc44-84f2615f0ef2";
  private final static String contuuid = "7aaef235-d8a8-478c-b4cd-82ee76734b16";
  private final static String svc = "0";


  private static void createFS(){
    Configuration conf = new Configuration();
    conf.set(Constants.DAOS_POOL_UUID, pooluuid);
    conf.set(Constants.DAOS_CONTAINER_UUID, contuuid);
    conf.set(Constants.DAOS_POOL_SVC, svc);
    fs = TestDaosTestUtils.createTestFileSystem(conf);
  }

  public static FileSystem getFS() {
    if(fs == null){
      createFS();
    }
    return fs;
  }
}
