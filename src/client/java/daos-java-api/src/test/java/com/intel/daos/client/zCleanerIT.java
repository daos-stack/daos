package com.intel.daos.client;

import org.junit.Test;

public class zCleanerIT {

  @Test
  public void clean() throws Exception{
    DaosFsClient.closeAll();
    DaosFsClient.daosFinalize();
  }
}
