package com.intel.daos.client;

import org.junit.AfterClass;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

import java.nio.ByteBuffer;
import java.util.Arrays;

public class DaosFileBasicIT {

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
  public void testMkdir()throws Exception{
    DaosFile daosFile = client.getFile("/root/");
    daosFile.mkdir();
  }

  @Test
  public void testMkdirs()throws Exception{
    DaosFile daosFile = client.getFile("/d1/d2/d3");
    daosFile.mkdirs();
    DaosFile parentFile = client.getFile("/d1/d2");
    String[] children = parentFile.listChildren();
    Assert.assertEquals(1, children.length);
  }

  @Test
  public void testRename()throws Exception{
    DaosFile dir1 = client.getFile("/src/dir");
    dir1.mkdirs();
    DaosFile srcFile = client.getFile(dir1, "data1");
    srcFile.createNewFile();

    DaosFile dir2 = client.getFile("/src/dir2");
    dir2.mkdirs();
    String destPath = dir2.getPath() + "/data2";
    DaosFile destFile = srcFile.rename(destPath);
    Assert.assertEquals(0, destFile.length());
  }

  @Test
  public void testWriteFile()throws Exception{
    DaosFile daosFile = client.getFile("/data");
    daosFile.createNewFile();
    int length = 100;
    ByteBuffer buffer = ByteBuffer.allocateDirect(length);
    byte[] bytes = new byte[length];
    for(int i=0; i<length; i++){
      bytes[i] = (byte)i;
    }
    buffer.put(bytes);

    daosFile.write(buffer, 0, 0, length);
    Assert.assertEquals(length, daosFile.length());
  }

  @Test
  public void testReadFile()throws Exception{
    DaosFile daosFile = client.getFile("/data2");
    daosFile.createNewFile();
    int length = 100;
    ByteBuffer buffer = ByteBuffer.allocateDirect(length);
    byte[] bytes = new byte[length];
    for(int i=0; i<length; i++){
      bytes[i] = (byte)i;
    }
    buffer.put(bytes);

    daosFile.write(buffer, 0, 0, length);

    ByteBuffer buffer2 = ByteBuffer.allocateDirect(length);
    daosFile.read(buffer2, 0, 0, length);
    byte[] bytes2 = new byte[length];
    buffer2.get(bytes2);
    Assert.assertTrue(Arrays.equals(bytes, bytes2));
    daosFile.release();
  }

  @Test
  public void testCreateNewFile()throws Exception{
    DaosFile daosFile = client.getFile("/zjf");
    daosFile.createNewFile();
  }

  @AfterClass
  public static void teardown()throws Exception{
    if(client != null) {
      client.disconnect();
    }
  }
}
