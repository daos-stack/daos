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
  public void testRenameFile()throws Exception{
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
  public void testRenameDir()throws Exception{
    DaosFile dir1 = client.getFile("/src2/dir");
    dir1.mkdirs();
    DaosFile srcFile = client.getFile(dir1, "subdir");
    srcFile.mkdir();

    DaosFile dir2 = client.getFile("/src2/dir2");
    dir2.mkdirs();
    String destPath = dir2.getPath() + "/subdir";
    DaosFile destFile = srcFile.rename(destPath);
    Assert.assertTrue(destFile.isDirectory());
  }

  @Test
  public void testVerifyEmptyDir()throws Exception{
    DaosFile dir1 = client.getFile("/src3/");
    dir1.mkdirs();
    String[] children = dir1.listChildren();
    Assert.assertEquals(0, children.length);
  }

  @Test
  public void testVerifyMultipleChildren()throws Exception{
    DaosFile dir1 = client.getFile("/src4/");
    dir1.mkdirs();
    DaosFile child1 = client.getFile(dir1, "c1");
    child1.createNewFile();
    DaosFile child2 = client.getFile(dir1, "c2");
    child2.mkdir();
    DaosFile child3 = client.getFile(dir1, "c3");
    child3.createNewFile();
    DaosFile child4 = client.getFile(dir1, "c4");
    child4.createNewFile();
    DaosFile child5 = client.getFile(dir1, "c5");
    child5.mkdir();
    String[] children = dir1.listChildren();
    Assert.assertEquals(5, children.length);
  }

  @Test
  public void testVerifyMultipleLongNameChildren()throws Exception{
    DaosFile dir1 = client.getFile("/src5/");
    dir1.mkdirs();
    for(int i=0; i<20; i++) {
      DaosFile child1 = client.getFile(dir1, i+"c10000000000000000000000000000000000c50000000000000000000000000000000000");
      if(i%2 == 0){
        child1.mkdir();
      }else{
        child1.createNewFile();
      }
    }

    String[] children = dir1.listChildren();
    Assert.assertEquals(20, children.length);
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

    long wl = daosFile.write(buffer, 0, 0, length);
    Assert.assertEquals(length, daosFile.length());
    Assert.assertEquals(length, wl);
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
