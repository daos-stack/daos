package com.intel.daos.client;

import org.junit.AfterClass;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

import java.nio.ByteBuffer;
import java.util.Arrays;

public class DaosFsClientIT {

  private static String poolId;
  private static String contId;

  private static DaosFsClient client;

  @BeforeClass
  public static void setup()throws Exception{
    poolId = "0eba76a4-5f9d-4c47-91c7-545b3677fb28";
    contId = "3f56f74f-dd21-49ec-899e-2b410543314b";

    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    client = builder.build();

    try {
      //clear all content
      DaosFile daosFile = client.getFile("/");
      String[] children = daosFile.listChildren();
      for(String child : children) {
        if(child.length() == 0 || ".".equals(child)){
          continue;
        }
        String path = "/"+child;
        DaosFile childFile = client.getFile(path);
        if(childFile.delete(true)){
          System.out.println("deleted folder "+path);
        }else{
          System.out.println("failed to delete folder "+path);
        }
      }
    }catch (Exception e){
      System.out.println("failed to clear/prepare file system");
      e.printStackTrace();
    }
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
