package io.daos.dfs;

import io.daos.DaosTestBase;
import org.junit.Assert;
import org.junit.Test;

import java.lang.reflect.Field;

public class DaosFsClientTestBase {

  public static DaosFsClient prepareFs(String poolId, String contId) throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = builder.build();

    try {
      //clear all content
      DaosFile daosFile = client.getFile("/");
      String[] children = daosFile.listChildren();
      for (String child : children) {
        if (child.length() == 0 || ".".equals(child)) {
          continue;
        }
        String path = "/" + child;
        DaosFile childFile = client.getFile(path);
        if (childFile.delete(true)) {
          System.out.println("deleted folder " + path);
        } else {
          System.out.println("failed to delete folder " + path);
        }
        childFile.release();
      }
      daosFile.release();
      return client;
    } catch (Exception e) {
      System.out.println("failed to clear/prepare file system");
      e.printStackTrace();
    }
    return null;
  }

  @Test
  public void testClone() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId("xyz").containerId("abc").defaultFileChunkSize(1000);
    DaosFsClient.DaosFsClientBuilder cloned = builder.clone();
    Assert.assertEquals("xyz", cloned.getPoolId());
    Assert.assertEquals("abc", cloned.getContId());

    Field field = DaosFsClient.DaosFsClientBuilder.class.getDeclaredField("defaultFileChunkSize");
    field.setAccessible(true);
    Assert.assertEquals(1000, (int) field.get(cloned));
  }

  public static void main(String args[]) throws Exception {
    DaosFsClient client = null;
    try {
      client = prepareFs(System.getProperty("pool_id", DaosTestBase.DEFAULT_POOL_ID),
          System.getProperty("cont_id", DaosTestBase.DEFAULT_CONT_ID));
    } finally {
      client.close();
    }
    if (client != null) {
      System.out.println("quitting");
    }

  }
}
