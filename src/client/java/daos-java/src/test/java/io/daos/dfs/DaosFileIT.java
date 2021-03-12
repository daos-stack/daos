package io.daos.dfs;

import com.sun.security.auth.module.UnixSystem;
import io.daos.*;
import org.junit.AfterClass;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.ByteBuffer;
import java.util.Arrays;

public class DaosFileIT {

  private static String poolId;
  private static String contId;

  private static DaosFsClient client;

  @BeforeClass
  public static void setup() throws Exception {
    poolId = DaosTestBase.getPoolId();
    contId = DaosTestBase.getContId();

    client = DaosFsClientTestBase.prepareFs(poolId, contId);
  }

  @Test
  public void testMkdir() throws Exception {
    DaosFile daosFile = client.getFile("/root/");
    daosFile.mkdir();
    Assert.assertTrue(daosFile.exists());
  }

  @Test
  public void testMkdirs() throws Exception {
    DaosFile daosFile = client.getFile("/d1/d2/d3");
    daosFile.mkdirs();
    DaosFile parentFile = client.getFile("/d1/d2");
    String[] children = parentFile.listChildren();
    Assert.assertEquals(1, children.length);
  }

  @Test
  public void testRenameFile() throws Exception {
    DaosFile dir1 = client.getFile("/src/dir");
    dir1.mkdirs();
    DaosFile srcFile = client.getFile(dir1, "data1");
    srcFile.createNewFile();

    DaosFile dir2 = client.getFile("/src/dir2");
    dir2.mkdirs();
    String destPath = dir2.getPath() + "/data2";
    DaosFile destFile = srcFile.rename(destPath);
    Assert.assertTrue(destFile.exists());
    Assert.assertEquals(0, destFile.length());
  }

  @Test
  public void testRenameToBeConfirmed() throws Exception {
    DaosFile dir1 = client.getFile("/src3/dir");
    dir1.mkdirs();
    DaosFile srcFile = client.getFile(dir1, "data1");
    srcFile.mkdir();

    DaosFile dir2 = client.getFile("/src3/dir2");
    dir2.mkdirs();
    String destPath = dir2.getPath() + "/data2";
    DaosFile destFile = client.getFile(destPath);
    destFile.createNewFile();
    DaosFile destFile2 = srcFile.rename(destPath);
    Assert.assertNotEquals(destFile.getObjId(), destFile2.getObjId());
    Assert.assertFalse(destFile.isDirectory());
    Assert.assertTrue(destFile2.isDirectory());
    String[] children = dir2.listChildren();
    Assert.assertEquals(1, children.length);
    Assert.assertEquals("data2", children[0]);
    DaosFile file3 = client.getFile(destPath);
    Assert.assertEquals(true, file3.isDirectory());
  }

  @Test
  public void testRenameDir() throws Exception {
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
  public void testVerifyEmptyDir() throws Exception {
    DaosFile dir1 = client.getFile("/src11/");
    dir1.mkdirs();
    String[] children = dir1.listChildren();
    Assert.assertEquals(0, children.length);
  }

  @Test
  public void testVerifyMultipleChildren() throws Exception {
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
  public void testVerifyMultipleLongNameChildren() throws Exception {
    DaosFile dir1 = client.getFile("/src5/");
    dir1.mkdirs();
    for (int i = 0; i < 20; i++) {
      DaosFile child1 = client.getFile(dir1, i + "c10000000000000000000000000000000000c50000000000000000000000000000000000");
      if (i % 2 == 0) {
        child1.mkdir();
      } else {
        child1.createNewFile();
      }
    }

    String[] children = dir1.listChildren();
    Assert.assertEquals(20, children.length);
  }

  @Test
  public void testWriteFile() throws Exception {
    DaosFile daosFile = client.getFile("/data");
    daosFile.createNewFile();
    int length = 100;
    ByteBuffer buffer = ByteBuffer.allocateDirect(length);
    byte[] bytes = new byte[length];
    for (int i = 0; i < length; i++) {
      bytes[i] = (byte) i;
    }
    buffer.put(bytes);

    long wl = daosFile.write(buffer, 0, 0, length);
    Assert.assertEquals(length, daosFile.length());
    Assert.assertEquals(length, wl);
  }

  @Test
  public void testReadFile() throws Exception {
    DaosFile daosFile = client.getFile("/data2");
    daosFile.createNewFile();
    int length = 100;
    ByteBuffer buffer = ByteBuffer.allocateDirect(length);
    byte[] bytes = new byte[length];
    for (int i = 0; i < length; i++) {
      bytes[i] = (byte) i;
    }
    buffer.put(bytes);

    daosFile.write(buffer, 0, 0, length);

    System.out.println(daosFile.length());

    ByteBuffer buffer2 = ByteBuffer.allocateDirect(length + 30);
    long actualLen = daosFile.read(buffer2, 0, 0, length + 30);
    Assert.assertEquals(length, actualLen);
    byte[] bytes2 = new byte[length];
    buffer2.get(bytes2);
    Assert.assertTrue(Arrays.equals(bytes, bytes2));
    daosFile.release();
  }

  @Test
  public void testCreateNewFileSimple() throws Exception {
    DaosFile daosFile = client.getFile("/zjf");
    daosFile.createNewFile();
  }

  @Test(expected = DaosIOException.class)
  public void testCreateNewFileWithCreateParentFalse() throws Exception {
    DaosFile daosFile = client.getFile("/d444/d3/d2/file");
    daosFile.createNewFile();
  }

  @Test
  public void testCreateNewFileWithCreateParentTrue() throws Exception {
    DaosFile daosFile = client.getFile("/d4/d3/d2/file");
    daosFile.createNewFile(true);
  }

  @Test
  public void testNotExists() throws Exception {
    DaosFile file = client.getFile("/zjf1");
    Assert.assertFalse(file.exists());
  }

  @Test
  public void testNotExistsAfterDeletion() throws Exception {
    DaosFile file = client.getFile("/zjf2");
    file.createNewFile();
    Assert.assertTrue(file.exists());
    file.delete();
    Assert.assertFalse(file.exists());
  }

  @Test
  public void testIsDirectoryFalse() throws Exception {
    DaosFile file = client.getFile("/zjf3");
    file.createNewFile();
    Assert.assertFalse(file.isDirectory());
  }

  @Test
  public void tesMkdirAndVerify() throws Exception {
    DaosFile file = client.getFile("/dir11");
    file.mkdir();
    Assert.assertTrue(file.isDirectory());
    DaosFile rootFile = client.getFile("/");
    rootFile.exists();
    String[] children = rootFile.listChildren();
    boolean found = false;
    for (String child : children) {
      if (file.getName().equals(child)) {
        found = true;
      }
    }
    Assert.assertTrue(found);
  }

  @Test(expected = IOException.class)
  public void testGetChunkSizeOfDir() throws Exception {
    DaosFile file = client.getFile("/dir1");
    file.mkdir();
    file.getChunkSize();
  }

  @Test
  public void testGetStatAttributesLength() throws Exception {
    DaosFile file = client.getFile("/zjf444");
    file.createNewFile();

    ByteBuffer buffer = ByteBuffer.allocateDirect(1024);
    String str = "ddddddddddddddddddddddddddddddddddddddddddddddddd";
    buffer.put(str.getBytes());
    file.write(buffer, 0, 0, str.length());
    StatAttributes attributes = file.getStatAttributes();
    Assert.assertEquals(str.length(), (int) attributes.getLength());
  }

  @Test
  public void testGetStatAttributes() throws Exception {
    DaosFile file = client.getFile("/zjf4");
    file.createNewFile();
    StatAttributes attributes = file.getStatAttributes();

    UnixSystem system = new UnixSystem();
    int uid = (int) system.getUid();
    int gid = (int) system.getGid();

    Assert.assertTrue(attributes.getUid() == uid);
    Assert.assertTrue(attributes.getGid() == gid);
    Assert.assertTrue(attributes.isFile());
    Assert.assertTrue(attributes.getLength() == 0);
    Assert.assertTrue(attributes.getObjId() != 0);
    Assert.assertTrue(attributes.getBlockCnt() == 0);
    Assert.assertTrue(attributes.getBlockSize() == 1024 * 1024);
    Assert.assertTrue(attributes.getMode() != 0);
    Assert.assertTrue(attributes.getAccessTime() != null);
    Assert.assertTrue(attributes.getModifyTime() != null);
    Assert.assertTrue(attributes.getCreateTime() != null);

    file = client.getFile("/zjf44");
    long time = System.currentTimeMillis();
    file.createNewFile(Constants.FILE_DEFAULT_FILE_MODE, DaosObjectType.OC_SX, 4096, false);
    attributes = file.getStatAttributes();
    Assert.assertTrue(attributes.getBlockSize() == 4096);

    long ctTime = DaosUtils.toMilliSeconds(attributes.getCreateTime());
    Assert.assertTrue(Math.abs(time - ctTime) < 1000);
    long mdTime = DaosUtils.toMilliSeconds(attributes.getModifyTime());
    Assert.assertTrue(Math.abs(time - mdTime) < 1000);
    long acTime = DaosUtils.toMilliSeconds(attributes.getAccessTime());
    Assert.assertTrue(Math.abs(time - acTime) < 1000);

    Assert.assertEquals(system.getUsername(), attributes.getUsername());

    Process process = Runtime.getRuntime().exec("groups");
    process.waitFor();
    try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
      String line = reader.readLine();
      Assert.assertEquals(line.split(" ")[0], attributes.getGroupname());
    }
  }

  @Test
  public void testSetGetExtAttribute() throws Exception {
    DaosFile file = client.getFile("/zjf5");
    file.createNewFile();
    file.setExtAttribute("att1", "xyz", Constants.SET_XATTRIBUTE_NO_CHECK);

    String value = file.getExtAttribute("att1", 3);
    Assert.assertEquals("xyz", value);

    value = file.getExtAttribute("att1", 4);
    Assert.assertEquals("xyz", value);
  }

  @Test(expected = DaosIOException.class)
  public void testRemoveExtAttribute() throws Exception {
    DaosFile file = client.getFile("/zjf6");
    file.mkdir();
    file.setExtAttribute("att1", "xyz", Constants.SET_XATTRIBUTE_CREATE);
    file.remoteExtAttribute("att1");
    file.getExtAttribute("att1", 3);
  }

  @Test
  public void testGetChunkSize() throws Exception {
    DaosFile file = client.getFile("/zjf7");
    file.createNewFile(0754, DaosObjectType.OC_SX, 2048, false);
    Assert.assertEquals(2048, file.getChunkSize());
  }

  @AfterClass
  public static void teardown() throws Exception {
    if (client != null) {
      client.close();
    }
  }
}
