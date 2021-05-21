package io.daos;

import org.junit.Assert;
import org.junit.Test;

public class DaosClientTest {

  @Test
  public void testBuilderClone() throws Exception {
    DaosClient.DaosClientBuilder builder = new DaosClient.DaosClientBuilder().poolId("pid")
        .containerId("cid");

    DaosClient.DaosClientBuilder bdCloned = builder.clone();
    Assert.assertNotEquals(builder, bdCloned);
    Assert.assertEquals(builder.getPoolId(), "pid");
    Assert.assertEquals(builder.getContId(), "cid");
  }
}
