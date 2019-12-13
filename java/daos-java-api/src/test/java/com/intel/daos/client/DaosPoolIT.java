package com.intel.daos.client;

import org.junit.AfterClass;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

import java.nio.ByteBuffer;
import java.util.Arrays;

public class DaosPoolIT {

  private static String poolId;
  private static String contId;

  private static DaosFsClient client;

  @BeforeClass
  public static void setup()throws Exception{
    poolId = System.getProperty("pool_id", DaosFsClientTestBase.DEFAULT_POOL_ID);
    contId = System.getProperty("cont_id", DaosFsClientTestBase.DEFAULT_CONT_ID);
  }

  @Test
  public void testPool() throws Exception{
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    client = builder.build();
    client.disconnect();
  }

  @AfterClass
  public static void teardown()throws Exception{
    if(client != null) {
      client.disconnect();
    }
  }
}
