package io.daos;

import org.junit.Test;

public class zCleanerIT {

  @Test
  public void clean() throws Exception {
    DaosClient.closeAll();
    DaosEventQueue.destroyAll();
    DaosClient.daosSafeFinalize();
  }
}
