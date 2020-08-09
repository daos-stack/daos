package io.daos.dfs;

import io.daos.DaosTestBase;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.IOException;

public class DaosFsClientIT {

  private static String poolId;
  private static String contId;

  @BeforeClass
  public static void setup() throws Exception {
    poolId = DaosTestBase.getPoolId();
    contId = DaosTestBase.getContId();
  }

  @Test
  public void testCreateFsClientFromSpecifiedContainer() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try {
      client = builder.build();
      Assert.assertTrue(client != null);
    } finally {
      if (client != null) {
        client.close();
      }
    }
  }

  @Test
  public void testCreateFsClientFromRootContainer() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId);
    DaosFsClient client = null;
    try {
      client = builder.build();
      Assert.assertTrue(client != null);
    } finally {
      if (client != null) {
        client.close();
      }
    }
  }

  @Test
  public void testFsClientCachePerPoolAndContainer() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    DaosFsClient client2[] = new DaosFsClient[1];
    try {
      client = builder.build();
      Thread thread = new Thread() {
        @Override
        public void run() {
          try {
            DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
            builder.poolId(poolId).containerId(contId);
            client2[0] = builder.build();
          } catch (IOException e) {
            e.printStackTrace();
          }
        }
      };
      thread.start();
      thread.join();
      Assert.assertEquals(client, client2[0]);
    } finally {
      client.close();
    }
  }

  @Test
  public void testDeleteSuccessful() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try {
      client = builder.build();
      Assert.assertTrue(client != null);
      client.delete("/ddddddd/zyx", true);
    } finally {
      if (client != null) {
        client.close();
      }
    }
  }

  @Test
  public void testMultipleMkdirs() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try {
      client = builder.build();
      Assert.assertTrue(client != null);
      client.mkdir("/mkdirs/1", true);
      client.mkdir("/mkdirs/1", true);
    } finally {
      if (client != null) {
        client.close();
      }
    }
  }

  @Test(expected = IOException.class)
  public void testMultipleMkdir() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try {
      client = builder.build();
      Assert.assertTrue(client != null);
      client.mkdir("/mkdir/1", false);
      client.mkdir("/mkdir/1", false);
    } finally {
      if (client != null) {
        client.close();
      }
    }
  }

  @Test(expected = IllegalArgumentException.class)
  public void testMoveWithOpenDirsIllegalSrcName() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try {
      String fileName = "srcFile/zb";
      client = builder.build();
      client.move(0, fileName, 0, "destFile");
    } finally {
      if (client != null) {
        client.close();
      }
    }
  }

  @Test(expected = IllegalArgumentException.class)
  public void testMoveWithOpenDirsIllegalDestName() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try {
      String fileName = "srcFile";
      client = builder.build();
      client.move(0, fileName, 0, "/destFile");
    } finally {
      if (client != null) {
        client.close();
      }
    }
  }

  @Test
  public void testMoveWithOpenDirs() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try {
      String fileName = "srcFile";
      client = builder.build();
      DaosFile srcDir = client.getFile("/mdir1");
      srcDir.mkdirs();
      DaosFile srcFile = client.getFile(srcDir, fileName);
      srcFile.createNewFile();

      DaosFile destDir = client.getFile("/mdir2");
      destDir.mkdirs();
      String destFileName = "destFile";
      client.move(srcDir.getObjId(), fileName, destDir.getObjId(), destFileName);
      Assert.assertFalse(srcFile.exists());
      Assert.assertTrue(client.getFile(destDir, destFileName).exists());
    } finally {
      if (client != null) {
        client.close();
      }
    }
  }
}
