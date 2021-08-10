package io.daos.dfs;

import io.daos.DaosIOException;
import io.daos.DaosTestBase;
import io.daos.dfs.uns.*;
import org.junit.*;

import java.io.File;
import java.nio.file.Files;

public class DaosUnsIT {

  private static String poolUuid;
  private static String contUuid;

  @BeforeClass
  public static void setup() {
    poolUuid = System.getProperty("pool_id", DaosTestBase.DEFAULT_POOL_ID);
    contUuid = System.getProperty("cont_id", DaosTestBase.DEFAULT_CONT_ID);
  }

  @Test
  public void testResolveDirectPathWithUuidsNoPrefix() throws Exception {
    String path = "/" + poolUuid + "/" + contUuid + "/abc/1234";
    DunsAttribute attribute = DaosUns.resolvePath(path);
    Assert.assertEquals(poolUuid, attribute.getPuuid());
    Assert.assertEquals(contUuid, attribute.getCuuid());
    Assert.assertEquals("/abc/1234", attribute.getRelPath());
    System.out.println(attribute.getLayoutType());
  }

  @Test
  public void testResolveDirectPathWithUuidsHasPrefix() throws Exception {
    String path = "daos://" + poolUuid + "/" + contUuid + "/abc/123";
    DunsAttribute attribute = DaosUns.resolvePath(path);
    Assert.assertEquals(poolUuid, attribute.getPuuid());
    Assert.assertEquals(contUuid, attribute.getCuuid());
    Assert.assertEquals("/abc/123", attribute.getRelPath());
  }

  @Test
  public void testResolveDirectPathWithUuidsRootPath() throws Exception {
    String path = "daos://" + poolUuid + "/" + contUuid;
    DunsAttribute attribute = DaosUns.resolvePath(path);
    Assert.assertEquals(poolUuid, attribute.getPuuid());
    Assert.assertEquals(contUuid, attribute.getCuuid());
    Assert.assertEquals("", attribute.getRelPath());
  }

  @Test(expected = DaosIOException.class)
  public void testResolvePathNotExistsFailed() throws Exception {
    File dir = Files.createTempDirectory("uns").toFile();
    File file = new File(dir, "path");
    try {
      DaosUns.resolvePath(file.getAbsolutePath());
    } finally {
      file.delete();
      dir.delete();
    }
  }

  @Test(expected = DaosIOException.class)
  public void testResolvePathWithoutAttributeFailed() throws Exception {
    File dir = Files.createTempDirectory("uns").toFile();
    File file = new File(dir, "path");
    file.mkdir();
    try {
      DaosUns.resolvePath(file.getAbsolutePath());
    } finally {
      file.delete();
      dir.delete();
    }
  }

  @Test
  public void testParseAttribute() throws Exception {
    String attrFmt = "DAOS.%s://%36s/%36s";
    String type = "POSIX";
    String attr = String.format(attrFmt, type, poolUuid, contUuid);
    DunsAttribute attribute = DaosUns.parseAttribute(attr);
    Assert.assertEquals(Layout.POSIX, attribute.getLayoutType());
    Assert.assertEquals(poolUuid, attribute.getPuuid());
    Assert.assertEquals(contUuid, attribute.getCuuid());

    type = "HDF5";
    attr = String.format(attrFmt, type, poolUuid, contUuid);
    attribute = DaosUns.parseAttribute(attr);
    Assert.assertEquals(Layout.HDF5, attribute.getLayoutType());
    Assert.assertEquals(poolUuid, attribute.getPuuid());
    Assert.assertEquals(contUuid, attribute.getCuuid());
  }

  @Test
  public void testSetAppInfoWithoutPath() throws Exception {
    Exception ee = null;
    try {
      DaosUns.setAppInfo("/abc1234567890abc", "user.attr", "abc");
    } catch (Exception e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("error code: 2 error msg: No such file or directory"));
  }

  @Test
  public void testSetAppInfoSuccessful() throws Exception {
    File file = Files.createTempDirectory("uns").toFile();
    try {
      DaosUns.setAppInfo(file.getAbsolutePath(), "user.attr", "abc");
      Assert.assertEquals("abc", DaosUns.getAppInfo(file.getAbsolutePath(), "user.attr",
          10));
    } finally {
      file.delete();
    }
  }

  @Test
  public void testRemoveAppInfoSuccessful() throws Exception {
    Exception ee = null;
    File file = Files.createTempDirectory("uns").toFile();
    try {
      DaosUns.setAppInfo(file.getAbsolutePath(), "user.attr", "abc");
      DaosUns.setAppInfo(file.getAbsolutePath(), "user.attr", null);
      DaosUns.getAppInfo(file.getAbsolutePath(), "user.attr", 10);
    } catch (Exception e) {
      ee = e;
    } finally {
      file.delete();
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("error code: 61 error msg: No data available"));
  }

  @Test
  public void testGetAppInfoBeforeSet() throws Exception {
    File file = Files.createTempDirectory("uns").toFile();
    Exception ee = null;
    try {
      DaosUns.getAppInfo(file.getAbsolutePath(), "user.attr",
          10);
    } catch (Exception e) {
      ee = e;
    } finally {
      file.delete();
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("error code: 61 error msg: No data available"));
  }
}
