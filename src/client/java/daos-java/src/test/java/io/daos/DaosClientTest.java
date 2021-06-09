package io.daos;

import io.daos.dfs.DaosFsClient;
import io.daos.dfs.DaosUns;
import io.daos.obj.DaosObjClient;
import org.junit.Assert;
import org.junit.Test;
import org.powermock.reflect.Whitebox;

public class DaosClientTest {

  @Test
  public void testBuilderClone() throws Exception {
    String pid = "aab99b21-5fba-402d-9ac0-59ce9f34f998";
    String cid = "70941ff5-44f3-4326-a5ec-b5b237df2f6f";
    DaosClient.DaosClientBuilder builder = new DaosClient.DaosClientBuilder().poolId(pid)
        .containerId(cid);

    DaosClient.DaosClientBuilder bdCloned = builder.clone();
    Assert.assertNotEquals(builder, bdCloned);
    Assert.assertEquals(bdCloned.getPoolId(), pid);
    Assert.assertEquals(bdCloned.getContId(), cid);

    DaosObjClient.DaosObjClientBuilder bobj = new DaosObjClient.DaosObjClientBuilder().poolId(pid).containerId(cid)
        .poolFlags(2);
    DaosObjClient.DaosObjClientBuilder bobjCloned = bobj.clone();
    Assert.assertEquals(bobjCloned.getPoolId(), pid);
    Assert.assertEquals(bobjCloned.getContId(), cid);
    Assert.assertEquals((int)Whitebox.getInternalState(bobjCloned, "poolFlags"), 2);

    DaosFsClient.DaosFsClientBuilder bfs = new DaosFsClient.DaosFsClientBuilder().poolId(pid).containerId(cid)
        .readOnlyFs(true);
    DaosFsClient.DaosFsClientBuilder bfsCloned = bfs.clone();
    Assert.assertEquals(bfsCloned.getPoolId(), pid);
    Assert.assertEquals(bfsCloned.getContId(), cid);
    Assert.assertEquals(Whitebox.getInternalState(bfsCloned, "readOnlyFs"), true);

    DaosUns.DaosUnsBuilder buns = new DaosUns.DaosUnsBuilder().poolId(pid).path("/root");
    DaosUns.DaosUnsBuilder bunsCloned = buns.clone();
    Assert.assertEquals(Whitebox.getInternalState(bunsCloned, "poolUuid"), pid);
    Assert.assertEquals(Whitebox.getInternalState(bunsCloned, "path"), "/root");
  }
}
