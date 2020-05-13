package io.daos.dfs;

import io.daos.dfs.uns.*;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.Map;

public class DaosUnsIT {

    private static String poolId;

    @BeforeClass
    public static void setup() {
        poolId = System.getProperty("pool_id", DaosFsClientTestBase.DEFAULT_POOL_ID);
    }

    private String createPath(String contId, Layout layout,
                              Map<PropType, DaosUns.PropValue> propMap,
                              File file) throws Exception {
        File dir = null;
        if (file == null) {
            dir = Files.createTempDirectory("uns").toFile();
            file = new File(dir, "path");
        }
        DaosUns.DaosUnsBuilder builder = new DaosUns.DaosUnsBuilder();
        builder.path(file.getAbsolutePath());
        builder.poolId(poolId);
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
        DaosUns uns = builder.build();
        try {
            String cid = uns.createPath();
            Assert.assertTrue(cid.length() > 0);
            return cid;
        } finally {
            if (dir != null) {
                file.delete();
                dir.delete();
            }
        }
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
    public void testCreateSimplePath() throws Exception {
        createPath((String)null);
    }

    @Test
    public void testCreatePathInExistingContFailed() throws Exception {
        String cid = createPath((String)null);
        Exception ee = null;
        try {
            createPath(cid);
        } catch (IOException e) {
            ee = e;
        }
        Assert.assertNotNull(ee);
        Assert.assertTrue(ee.getMessage().contains(cid));
        Assert.assertTrue(ee.getMessage().contains("error code: 17"));
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
        int perms =  permGet | permDel | permSet;
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
        int perms =  permGet | permDel | permSet;
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

    @Test
    public void testDestroyPath() throws Exception {
        File dir = Files.createTempDirectory("uns").toFile();
        File file = new File(dir, "path");
        try {
            createPath(null, null, null, file);
            DaosUns.resolvePath(file.getAbsolutePath());

            DaosUns.DaosUnsBuilder builder = new DaosUns.DaosUnsBuilder();
            builder.path(file.getAbsolutePath());
            builder.poolId(poolId);
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
        String attr = String.format(attrFmt, type, DaosFsClientTestBase.DEFAULT_POOL_ID,
                DaosFsClientTestBase.DEFAULT_CONT_ID);
        DunsAttribute attribute = DaosUns.parseAttribute(attr);
        Assert.assertEquals(Layout.POSIX, attribute.getLayoutType());
        Assert.assertEquals(DaosFsClientTestBase.DEFAULT_POOL_ID, attribute.getPuuid());
        Assert.assertEquals(DaosFsClientTestBase.DEFAULT_CONT_ID, attribute.getCuuid());

        type = "HDF5";
        attr = String.format(attrFmt, type, DaosFsClientTestBase.DEFAULT_POOL_ID,
                DaosFsClientTestBase.DEFAULT_CONT_ID);
        attribute = DaosUns.parseAttribute(attr);
        Assert.assertEquals(Layout.HDF5, attribute.getLayoutType());
        Assert.assertEquals(DaosFsClientTestBase.DEFAULT_POOL_ID, attribute.getPuuid());
        Assert.assertEquals(DaosFsClientTestBase.DEFAULT_CONT_ID, attribute.getCuuid());
    }
}
