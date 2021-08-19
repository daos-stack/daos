package io.daos.dfs;

import io.daos.dfs.uns.*;
import org.junit.Assert;
import org.junit.Test;

import java.io.File;

public class DaosUnsTest {

  @Test(expected = IllegalArgumentException.class)
  public void testBuilderWithoutPath() throws Exception {
    DaosUns.DaosUnsBuilder builder = new DaosUns.DaosUnsBuilder();
    builder.build();
  }

  @Test(expected = IllegalArgumentException.class)
  public void testBuilderWithoutPoolId() throws Exception {
    DaosUns.DaosUnsBuilder builder = new DaosUns.DaosUnsBuilder();
    builder.path("/abc");
    builder.build();
  }

  @Test(expected = IllegalArgumentException.class)
  public void testBuilderWithWrongLayout() throws Exception {
    DaosUns.DaosUnsBuilder builder = new DaosUns.DaosUnsBuilder();
    builder.path("/abc");
    builder.poolId("4567-rty-456");
    builder.layout(Layout.UNKNOWN);
    builder.build();
  }

  @Test(expected = IllegalArgumentException.class)
  public void testBuilderWithWrongLayout2() throws Exception {
    DaosUns.DaosUnsBuilder builder = new DaosUns.DaosUnsBuilder();
    builder.path("/abc");
    builder.poolId("4567-rty-456");
    builder.layout(Layout.UNRECOGNIZED);
    builder.build();
  }

  @Test
  public void testBuilderSimple() throws Exception {
    DaosUns.DaosUnsBuilder builder = new DaosUns.DaosUnsBuilder();
    builder.path("/abc");
    builder.poolId("4567-rty-456");
    DaosUns uns = builder.build();
    Assert.assertEquals("/abc", uns.getPath());
    Assert.assertEquals("4567-rty-456", uns.getPoolUuid());
  }

  @Test
  public void testRootFile() throws Exception {
    File file = new File("/abc");
    Assert.assertNull(file.getParentFile().getParentFile());
  }

  @Test
  public void testSetAppInfoBadAttrName() throws Exception {
    Exception ee = null;
    try {
      DaosUns.setAppInfo("/abc", "attr", "abc");
    } catch (Exception e) {
      ee = e;
    }
    Assert.assertTrue(ee instanceof IllegalArgumentException);
    Assert.assertTrue(ee.getMessage().contains("start with \"user.\""));
  }

  @Test
  public void testGetAppInfoBadAttrName() throws Exception {
    Exception ee = null;
    try {
      DaosUns.getAppInfo("/abc", "attr", 100);
    } catch (Exception e) {
      ee = e;
    }
    Assert.assertTrue(ee instanceof IllegalArgumentException);
    Assert.assertTrue(ee.getMessage().contains("start with \"user.\""));
  }
}
