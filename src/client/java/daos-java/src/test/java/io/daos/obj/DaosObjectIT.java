package io.daos.obj;

import io.daos.DaosTestBase;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.IOException;
import java.util.concurrent.atomic.AtomicInteger;

public class DaosObjectIT {

  private static String poolId = DaosTestBase.getPoolId();
  private static String contId = DaosTestBase.getObjectContId();

  private static DaosObjClient client;

  private static AtomicInteger lowSeq = new AtomicInteger();

  @BeforeClass
  public static void beforeClass() throws IOException {
    client = new DaosObjClient.DaosObjClientBuilder().poolId(poolId).containerId(contId).build();
  }

  @Test
  public void testObjectOpen() throws IOException {
    DaosObjectId id = new DaosObjectId(0, lowSeq.incrementAndGet());
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
    } finally {
      object.close();
    }
  }

  @AfterClass
  public static void afterClass() throws IOException {
    if (client != null) {
      client.close();
    }
  }
}
