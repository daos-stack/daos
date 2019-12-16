package com.intel.daos.client;

import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

public class DaosFsClientIT {

  private static String poolId;
  private static String contId;

  @BeforeClass
  public static void setup()throws Exception{
    poolId = System.getProperty("pool_id", DaosFsClientTestBase.DEFAULT_POOL_ID);
    contId = System.getProperty("cont_id", DaosFsClientTestBase.DEFAULT_CONT_ID);
  }

  @Test
  public void testCreateFsClientFromSpecifiedContainer() throws Exception{
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try{
      client = builder.build();
      Assert.assertTrue(client != null);
    }finally {
      if(client != null){
        client.disconnect();
      }
    }
  }

  @Test
  public void testCreateFsClientFromRootContainer() throws Exception{
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId);
    DaosFsClient client = null;
    try{
      client = builder.build();
      Assert.assertTrue(client != null);
    }finally {
      if(client != null){
        client.disconnect();
      }
    }
  }

  @Test
  public void testCreateNewPoolWithoutScmSize()throws Exception{
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    Exception ee = null;
    DaosFsClient client = null;
    try {
      client = builder.build();
    }catch (Exception e){
      ee = e;
    }finally {
      if(client != null){
        client.disconnect();
      }
    }
    Assert.assertTrue(ee instanceof DaosIOException);
    DaosIOException de = (DaosIOException)ee;
    Assert.assertEquals(Constants.CUSTOM_ERR_NO_POOL_SIZE.getCode(), de.getErrorCode());
  }

  @Test
  public void testCreateNewPool()throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolScmSize(1*1024*1024*1024);
    DaosFsClient client = null;
    try{
      client = builder.build();
      Assert.assertTrue(client != null);
    }finally {
      if(client != null){
        client.disconnect();
        DaosFsClient.destroyPool(Constants.POOL_DEFAULT_SERVER_GROUP, client.getPoolId(), true);
      }
    }
  }
}
