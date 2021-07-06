package io.daos.dfs;

import io.daos.DaosIOException;
import io.daos.DaosTestBase;
import io.daos.dfs.uns.*;
import org.junit.*;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.Map;

public class DaosUnsIT {

  private static String poolUuid;
  private static String contUuid;

  private DaosUns uns;
  private File dir;

  @BeforeClass
  public static void setup() {
    poolUuid = System.getProperty("pool_id", DaosTestBase.DEFAULT_POOL_ID);
    contUuid = System.getProperty("cont_id", DaosTestBase.DEFAULT_CONT_ID);
  }

  @Before
  public void prepare() {
    uns = null;
    dir = null;
  }

  @After
  public void done() throws Exception {
    if (uns != null) {
      try {
        uns.destroyPath();
      } catch (Exception e) {

      }
    }
    if (dir != null) {
      dir.delete();
    }
  }

  private String createPath(String contId, Layout layout,
                            Map<PropType, DaosUns.PropValue> propMap,
                            File file) throws Exception {
    File dir2 = null;
    if (file == null) {
      dir2 = Files.createTempDirectory("uns").toFile();
      file = new File(dir2, "path");
    }
    DaosUns.DaosUnsBuilder builder = new DaosUns.DaosUnsBuilder();
    builder.path(file.getAbsolutePath());
    builder.poolId(poolUuid);
    if (contId != null) {
      builder.containerId(contId);
    }
    if (layout != null) {
      builder.layout(layout);
    }
    if (propMap != null && !propMap.isEmpty()) {
      for (Map.Entry<PropType, DaosUns.PropValue> entry : propMap.entrySet()) {
        builder.putEntry(entry.getKey(), entry.getValue());
      }
    }
    DaosUns duns = builder.build();
    String cid;
    try {
      cid = duns.createPath();
      Assert.assertTrue(cid.length() > 0);
    } catch (Exception e) {
      if (file != null) {
        file.delete();
      }
      if (dir2 != null) {
        dir2.delete();
      }
      throw e;
    }
    uns = duns;
    return cid;
  }

  private String createPath(String contId, Layout layout,
                            Map<PropType, DaosUns.PropValue> propMap) throws Exception {
    return createPath(contId, layout, propMap, null);
  }

  private String createPath(String contId) throws Exception {
    return createPath(contId, null, null);
  }

  private String createPath(Map<PropType, DaosUns.PropValue> propMap) throws Exception {
    return createPath(null, null, propMap);
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

  @Test
  public void testCreateSimplePath() throws Exception {
    createPath((String) null);
  }

  @Test
  public void testCreatePathInExistingContFailed() throws Exception {
    String cid = createPath((String) null);
    Exception ee = null;
    try {
      createPath(cid);
    } catch (IOException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains(cid));
    Assert.assertTrue(ee.getMessage().contains("error code: 5"));
  }

  @Test
  public void testCreatePathLayoutHDF5() throws Exception {
    String cid = createPath(null, Layout.HDF5, null);
    Assert.assertTrue(cid.length() > 0);
  }

  @Test
  public void testCreatePathWithPropertiesString() throws Exception {
    Map<PropType, DaosUns.PropValue> propMap = new HashMap<>();
    propMap.put(PropType.DAOS_PROP_CO_LABEL, new DaosUns.PropValue("label", 0));
    String cid = createPath(propMap);
    Assert.assertTrue(cid.length() > 0);
  }

  @Test
  public void testCreatePathWithPropertiesInteger() throws Exception {
    Map<PropType, DaosUns.PropValue> propMap = new HashMap<>();
    propMap.put(PropType.DAOS_PROP_CO_LAYOUT_VER, new DaosUns.PropValue(2L, 0));
    String cid = createPath(propMap);
    Assert.assertTrue(cid.length() > 0);
  }

  private void createPathWithAcls(boolean inOrder) throws Exception {
    Map<PropType, DaosUns.PropValue> propMap = new HashMap<>();
    /* extracted from daos_container.c */
    int aclOwner = 0;
    int aclUser = 1;
    String user = new com.sun.security.auth.module.UnixSystem().getUsername() + "@";
    int accessAllow = 1;
    int permDel = 1 << 3;
    int permGet = 1 << 6;
    int permSet = 1 << 7;
    int perms = permGet | permDel | permSet;
    DaosAce ace = DaosAce.newBuilder()
        .setAccessTypes(accessAllow)
        .setPrincipal(user)
        .setPrincipalType(aclUser)
        .setPrincipalLen(user.length())
        .setAllowPerms(perms)
        .build();
    DaosAce ace2 = DaosAce.newBuilder()
        .setAccessTypes(accessAllow)
        .setPrincipalType(aclOwner)
        .setAllowPerms(perms)
        .setPrincipalLen(0)
        .build();
    DaosAcl.Builder aclBuilder = DaosAcl.newBuilder()
        .setVer(1);
    if (inOrder) {
      aclBuilder.addAces(ace2).addAces(ace);
    } else {
      aclBuilder.addAces(ace).addAces(ace2);
    }

    DaosAcl acl = aclBuilder.build();
    propMap.put(PropType.DAOS_PROP_CO_ACL, new DaosUns.PropValue(acl, 0));
    String cid = createPath(null, Layout.HDF5, propMap);
    Assert.assertTrue(cid.length() > 0);
  }

  @Test
  public void testCreatePathWithPropertiesAclWrongOrder() throws Exception {
    Exception ee = null;
    try {
      createPathWithAcls(false);
    } catch (IOException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("ACEs out of order"));
  }

  @Test
  public void testCreatePathWithPropertiesAclUserAndAclOwner() throws Exception {
    createPathWithAcls(true);
  }

  @Test
  public void testCreatePathWithPropertiesAclOwner() throws Exception {
    Map<PropType, DaosUns.PropValue> propMap = new HashMap<>();
    /* extracted from daos_container.c */
    int aclOwner = 0;
    int accessAllow = 1;
    int permDel = 1 << 3;
    int permGet = 1 << 6;
    int permSet = 1 << 7;
    int perms = permGet | permDel | permSet;
    DaosAce ace = DaosAce.newBuilder()
        .setAccessTypes(accessAllow)
        .setPrincipalType(aclOwner)
        .setAllowPerms(perms)
        .build();
    DaosAcl acl = DaosAcl.newBuilder()
        .setVer(1)
        .addAces(ace).build();
    propMap.put(PropType.DAOS_PROP_CO_ACL, new DaosUns.PropValue(acl, 0));
    String cid = createPath(null, Layout.HDF5, propMap);
    Assert.assertTrue(cid.length() > 0);
  }

  @Test
  public void testResolvePath() throws Exception {
    File dir = Files.createTempDirectory("uns").toFile();
    File file = new File(dir, "path");
    try {
      String cid = createPath(null, null, null, file);
      DunsAttribute attribute = DaosUns.resolvePath(file.getAbsolutePath());
      Assert.assertEquals(cid, attribute.getCuuid());
      Assert.assertEquals(Layout.POSIX, attribute.getLayoutType());
    } finally {
      file.delete();
      dir.delete();
    }
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
  public void testDestroyPath() throws Exception {
    File dir = Files.createTempDirectory("uns").toFile();
    File file = new File(dir, "path");
    try {
      createPath(null, null, null, file);
      DaosUns.resolvePath(file.getAbsolutePath());

      DaosUns.DaosUnsBuilder builder = new DaosUns.DaosUnsBuilder();
      builder.path(file.getAbsolutePath());
      builder.poolId(poolUuid);
      DaosUns uns = builder.build();
      uns.destroyPath();

      Assert.assertTrue(!file.exists());
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
