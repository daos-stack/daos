package com.intel.daos.client;

import org.junit.AfterClass;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

public class DaosFileMoreIT {

  private static String poolId;
  private static String contId;

  private static DaosFsClient client;

  @BeforeClass
  public static void setup()throws Exception{
    poolId = System.getProperty("pool_id", DaosFsClientTestBase.DEFAULT_POOL_ID);
    contId = System.getProperty("cont_id", DaosFsClientTestBase.DEFAULT_CONT_ID);
    client = DaosFsClientTestBase.prepareFs(poolId, contId);
  }

  @Test
  public void testNotExists()throws Exception{
    DaosFile file = client.getFile("/zjf1");
    Assert.assertFalse(file.exists());
  }

  @Test
  public void testNotExistsAfterDeletion()throws Exception{
    DaosFile file = client.getFile("/zjf2");
    file.createNewFile();
    Assert.assertTrue(file.exists());
    file.delete();
    Assert.assertFalse(file.exists());
  }

  @Test
  public void testIsDirectoryFalse() throws Exception{
    DaosFile file = client.getFile("/zjf3");
    file.createNewFile();
    Assert.assertFalse(file.isDirectory());
  }

  @Test
  public void tesMkdirAndVerify() throws Exception{
    DaosFile file = client.getFile("/dir1");
    file.mkdir();
    Assert.assertTrue(file.isDirectory());
  }

  @AfterClass
  public static void teardown()throws Exception{
    if(client != null){
      client.disconnect();
    }
  }
}
