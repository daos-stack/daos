package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.Constants;
import io.daos.DaosTestBase;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;
import sun.nio.ch.DirectBuffer;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
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

  @Test
  public void testObjectUpdate() throws IOException {
    DaosObjectId id = new DaosObjectId(100, 342900);
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      list.add(createEntry(object,"akey1", 10, 0, 30, true));
//      list.add(createEntry(object,"akey2", 10, 0, 30, true));
      IODataDesc desc = object.createDataDesc("dkey1", list);
      object.update(desc);
    } finally {
      object.punch();
      object.close();
    }
  }

  @Test
  public void testObjectFetch() throws IOException {
    DaosObjectId id = new DaosObjectId(100, 344900);
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      list.add(createEntry(object,"akey1", 10, 0, 30, true));
//      list.add(createEntry(object,"akey2", 10, 0, 30, true));
      IODataDesc desc = object.createDataDesc("dkey1", list);
      object.update(desc);
      // fetch
      List<IODataDesc.Entry> list2 = new ArrayList<>();
      list2.add(createEntry(object,"akey1", 10, 0, 30, false));
//      list.add(createEntry(object,"akey2", 10, 0, 30, true));
      IODataDesc desc2 = object.createDataDesc("dkey1", list2);
      object.fetch(desc2);
    } finally {
      object.punch();
      object.close();
    }
  }

  @Test
  public void testPunchDkeys() throws IOException {
    DaosObjectId id = new DaosObjectId(100, 344800);
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      list.add(createEntry(object,"akey1", 10, 0, 30, true));
      IODataDesc desc = object.createDataDesc("dkey1", list);
      object.update(desc);
//      List<IODataDesc.Entry> list2 = new ArrayList<>();
//      list2.add(createEntry(object,"akey1", 10, 0, 30, true));
//      IODataDesc desc2 = object.createDataDesc("dkey2", list2);
//      object.update(desc2);
      // punch dkeys
      object.punchDkeys(Arrays.asList("dkey1"));
    } finally {
      object.punch();
      object.close();
    }
  }

  @Test
  public void testPunchAkeys() throws IOException {
    DaosObjectId id = new DaosObjectId(100, 444800);
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      list.add(createEntry(object,"akey1", 10, 0, 30, true));
      IODataDesc desc = object.createDataDesc("dkey1", list);
      object.update(desc);
//      List<IODataDesc.Entry> list2 = new ArrayList<>();
//      list2.add(createEntry(object,"akey1", 10, 0, 30, true));
//      IODataDesc desc2 = object.createDataDesc("dkey2", list2);
//      object.update(desc2);
      // punch akeys
      object.punchAkeys("dkey1", Arrays.asList("akey1"));
    } finally {
      object.punch();
      object.close();
    }
  }

  @Test
  public void testListDkeys() throws IOException {
    DaosObjectId id = new DaosObjectId(100, 544800);
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      list.add(createEntry(object,"akey1", 10, 0, 30, true));
      IODataDesc desc = object.createDataDesc("dkey1", list);
      object.update(desc);
      List<IODataDesc.Entry> list2 = new ArrayList<>();
      list2.add(createEntry(object,"akey1", 10, 0, 30, true));
      IODataDesc desc2 = object.createDataDesc("dkey2", list2);
      object.update(desc2);
      // list dkeys
      IOKeyDesc keyDesc = object.createKD(null);
      object.listDkeys(keyDesc);
    } finally {
      object.punch();
      object.close();
    }
  }

  @Test
  public void testListAkeys() throws Exception {
    DaosObjectId id = new DaosObjectId(100, 744800);
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      list.add(createEntry(object,"akey1", 10, 0, 30, true));
      IODataDesc desc = object.createDataDesc("dkey1", list);
      object.update(desc);
//      List<IODataDesc.Entry> list2 = new ArrayList<>();
//      list2.add(createEntry(object,"akey2", 10, 0, 30, true));
//      IODataDesc desc2 = object.createDataDesc("dkey1", list2);
//      object.update(desc2);
      // list akeys
      Thread.sleep(1000);
      IOKeyDesc keyDesc = object.createKD("dkey1");
      object.listAkeys(keyDesc);
    } finally {
      object.punch();
      object.close();
    }
  }

  private IODataDesc.Entry createEntry(DaosObject object, String akey, int recordSize, int offset, int dataSize,
                                       boolean updateOrFetch) throws IOException{
    if (updateOrFetch) {
      ByteBuffer buffer = BufferAllocator.directBuffer(dataSize);
      for (int i = 0; i < dataSize; i++) {
        buffer.put((byte)((i + 33) % 128));
      }
      buffer.flip();
      IODataDesc.Entry entry = object.createEntryForUpdate(akey, IODataDesc.IodType.ARRAY, offset,
        recordSize, buffer);
      return entry;
    }
    return object.createEntryForFetch(akey, IODataDesc.IodType.ARRAY, offset, recordSize, dataSize);
  }

//  @Test
//  public void testGetDataTypesSizes() throws IOException {
//    ByteBuffer buffer = BufferAllocator.directBuffer(8);
//    buffer.order(Constants.DEFAULT_ORDER);
//    int size = client.getDataTypesSizes(((DirectBuffer)buffer).address());
//    buffer.limit(size);
//    System.out.println(buffer.getShort());
//    System.out.println(buffer.getShort());
//    System.out.println(buffer.getShort());
//    System.out.println(buffer.getShort());
//  }

  @AfterClass
  public static void afterClass() throws IOException {
    if (client != null) {
      client.close();
    }
  }
}
