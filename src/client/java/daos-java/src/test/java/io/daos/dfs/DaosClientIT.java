package io.daos.dfs;

import io.daos.DaosClient;
import io.daos.DaosIOException;
import io.daos.DaosTestBase;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class DaosClientIT {

  private static String poolId;
  private static String contId;

  @BeforeClass
  public static void setup() throws Exception {
    poolId = DaosTestBase.getPoolId();
    contId = DaosTestBase.getContId();
  }

  @Test
  public void testSetGetAttributes() throws Exception {
    DaosClient.DaosClientBuilder builder = new DaosClient.DaosClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosClient client = null;
    try {
      client = (DaosClient) builder.build();
      Assert.assertTrue(client != null);
      Map<String, String> map = new HashMap<>();
      map.put("daos.spark.buffer-size", "1024");
      map.put("daos.spark.async", "true");
      client.setAttributes(map);
      List<String> nameList = new ArrayList<>();
      nameList.addAll(map.keySet());
      Map<String, String> retMap = client.getAttributes(nameList);
      Assert.assertEquals(2, retMap.size());
      for (Map.Entry<String, String> entry : retMap.entrySet()) {
        Assert.assertEquals(entry.getValue(), map.get(entry.getKey()));
      }
    } finally {
      if (client != null) {
        client.close();
      }
    }
  }

  @Test(expected = DaosIOException.class)
  public void testSetGetMissingAttributes() throws Exception {
    DaosClient.DaosClientBuilder builder = new DaosClient.DaosClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosClient client = null;
    try {
      client = (DaosClient) builder.build();
      Assert.assertTrue(client != null);
      Map<String, String> map = new HashMap<>();
      map.put("wdaos.spark.buffer-sizew", "1024");
      map.put("wdaos.spark.asyncw", "true");
      client.setAttributes(map);
      List<String> nameList = new ArrayList<>();
      nameList.addAll(map.keySet());
      String name = "daos.missing";
      nameList.add(name);
      client.getAttributes(nameList);
    } finally {
      if (client != null) {
        client.close();
      }
    }
  }

  @Test
  public void testListAttributes() throws Exception {
    DaosClient.DaosClientBuilder builder = new DaosClient.DaosClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosClient client = null;
    try {
      client = (DaosClient) builder.build();
      Assert.assertTrue(client != null);
      Map<String, String> map = new HashMap<>();
      map.put("xdaos.spark.buffer-sizew", "1024");
      map.put("ydaos.spark.asyncw", "true");
      map.put("zdaos.spark.asyncw", "true");
      client.setAttributes(map);
      Set<String> nameSet = client.listAttributes();
      nameSet.containsAll(map.keySet());
    } finally {
      if (client != null) {
        client.close();
      }
    }
  }

  @Test
  public void testRetrieveUserAttributes() throws Exception {
    DaosClient.DaosClientBuilder builder = new DaosClient.DaosClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosClient client = null;
    try {
      client = (DaosClient) builder.build();
      Assert.assertTrue(client != null);
      Map<String, String> map = new HashMap<>();
      map.put("to-list1", "1");
      map.put("to-list2", "2");
      map.put("to-list3", "3");
      client.setAttributes(map);
      client.refreshUserDefAttrs();
      Map<String, String> attrMap = client.getUserDefAttrMap();
      for (Map.Entry<String, String> entry : map.entrySet()) {
        Assert.assertEquals(entry.getValue(), attrMap.get(entry.getKey()));
      }
      Exception ee = null;
      try {
        attrMap.put("sx", "value");
      } catch (Exception e) {
        ee = e;
      }
      Assert.assertTrue(ee instanceof UnsupportedOperationException);
    } finally {
      if (client != null) {
        client.close();
      }
    }
  }
}
