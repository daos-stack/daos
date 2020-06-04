package io.daos;

import io.daos.dfs.DaosFsClient;
import io.daos.dfs.DaosFsClientTestBase;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

public class SharableClientIT {
    private static String poolId;
    private static String contId;

    @BeforeClass
    public static void setup()throws Exception{
        poolId = System.getProperty("pool_id", DaosFsClientTestBase.DEFAULT_POOL_ID);
        contId = System.getProperty("cont_id", DaosFsClientTestBase.DEFAULT_CONT_ID);
    }

    @Test
    public void testFsClientReferenceOne() throws Exception {
        DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
        builder.poolId(poolId).containerId(contId);
        DaosFsClient client = null;
        try{
            client = builder.build();
            Assert.assertEquals(1, client.getRefCnt());
        }finally {
            if(client != null){
                client.close();
                Assert.assertEquals(0, client.getRefCnt());
            }
        }
        Exception ee = null;
        try {
            client.incrementRef();
        } catch (Exception e) {
            ee = e;
        }
        Assert.assertTrue(ee instanceof IllegalStateException);
    }

    @Test
    public void testFsClientReferenceMore() throws Exception {
        DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
        builder.poolId(poolId).containerId(contId);
        DaosFsClient client = null;
        int cnt = 0;
        try{
            client = builder.build();
            cnt = client.getRefCnt();
            builder.build();
            Assert.assertEquals(cnt + 1, client.getRefCnt());
            client.close();
            client.incrementRef();
            Assert.assertEquals(cnt + 1, client.getRefCnt());
            client.decrementRef();
        } catch (Exception e){
            e.printStackTrace();
        }finally {
            if(client != null){
                client.close();
                Assert.assertEquals(cnt - 1, client.getRefCnt());
            }
        }
    }
}
