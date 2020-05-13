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
        builder.poolUuid(poolId);
        if (contId != null) {
            builder.contUuid(contId);
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

    @Test
    public void testCreatePathWithPropertiesAcl() throws Exception {
        Map<PropType, DaosUns.PropValue> propMap = new HashMap<>();
        DaosAce ace = DaosAce.newBuilder()
                        .setAccessTypes(1)
                        .setPrincipalType(0)
                        .setPrincipalLen(10)
                        .setAccessFlags(2)
                        .setPrincipal("1234567890").build();
        byte[] bytes = ace.toByteArray();
        DaosAcl acl = DaosAcl.newBuilder()
                        .setLen(bytes.length)
                        .addAces(ace).build();
        propMap.put(PropType.DAOS_PROP_CO_ACL, new DaosUns.PropValue(acl, 0));
        String cid = createPath(propMap);
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
            builder.poolUuid(poolId);
            DaosUns uns = builder.build();
            uns.destroyPath();

            Assert.assertTrue(!file.exists());
        } finally {
            file.delete();
            dir.delete();
        }
    }
}
