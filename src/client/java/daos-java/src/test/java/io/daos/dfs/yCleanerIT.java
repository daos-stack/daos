package io.daos.dfs;

import org.junit.Test;

public class yCleanerIT {

  @Test
  public void clean() throws Exception {
    DaosFsClient.closeAll();
  }
}
