package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.Constants;
import io.daos.DaosTestBase;
import org.junit.AfterClass;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;
import java.util.concurrent.atomic.AtomicInteger;

public class DaosObjectIT {

  private static String poolId = DaosTestBase.getPoolId();
  private static String contId = DaosTestBase.getObjectContId();

  private static DaosObjClient client;

  private static AtomicInteger lowSeq = new AtomicInteger();

  private final Random random = new Random();

  @BeforeClass
  public static void beforeClass() throws IOException {
    client = new DaosObjClient.DaosObjClientBuilder().poolId(poolId).containerId(contId).build();
  }

  @Test
  public void testObjectOpen() throws IOException {
    DaosObjectId id = new DaosObjectId(random.nextInt(), lowSeq.incrementAndGet());
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      Assert.assertTrue(object.isOpen());
    } finally {
      object.close();
    }
  }

  @Test
  public void testObjectUpdateAndFetch() throws IOException {
    DaosObjectId id = new DaosObjectId(random.nextInt(), lowSeq.incrementAndGet());
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      Assert.assertTrue(object.isOpen());
      List<IODataDesc.Entry> list = new ArrayList<>();
      int dataSize = 30;
      byte[] bytes = generateDataArray(dataSize);
      list.add(createEntryForUpdate("akey1", 10, 0, dataSize, bytes));
      list.add(createEntryForUpdate("akey2", 10, 0, dataSize, bytes));
      IODataDesc desc = object.createDataDescForUpdate("dkey1", list);
      object.update(desc);
      // fetch akey1
      List<IODataDesc.Entry> list2 = new ArrayList<>();
      list2.add(createEntryForFetch("akey1", 10, 0, 80));
      IODataDesc desc2 = object.createDataDescForFetch("dkey1", list2);
      object.fetch(desc2);
      IODataDesc.Entry entry = list2.get(0);
      Assert.assertEquals(dataSize, entry.getActualSize());
      byte[] actualBytes = new byte[dataSize];
      entry.get(actualBytes);
      Assert.assertTrue(Arrays.equals(bytes, actualBytes));
      // fetch akey2
      list2.clear();
      list2.add(createEntryForFetch("akey2", 10, 0, 30));
      desc2 = object.createDataDescForFetch("dkey1", list2);
      object.fetch(desc2);
      entry = list2.get(0);
      Assert.assertEquals(dataSize, entry.getActualSize());
      entry.get(actualBytes);
      Assert.assertTrue(Arrays.equals(bytes, actualBytes));
      // fetch both
      list2.clear();
      list2.add(createEntryForFetch("akey1", 10, 0, 50));
      list2.add(createEntryForFetch("akey2", 10, 0, 60));
      desc2 = object.createDataDescForFetch("dkey1", list2);
      object.fetch(desc2);
      IODataDesc.Entry entry1 = list2.get(0);
      Assert.assertEquals(dataSize, entry1.getActualSize());
      entry1.get(actualBytes);
      Assert.assertTrue(Arrays.equals(bytes, actualBytes));
      IODataDesc.Entry entry2 = list2.get(1);
      Assert.assertEquals(dataSize, entry2.getActualSize());
      entry2.get(actualBytes);
      Assert.assertTrue(Arrays.equals(bytes, actualBytes));
    } finally {
      object.punch();
      object.close();
    }
  }

  @Test
  public void testPunchAndListDkeys() throws IOException {
    DaosObjectId id = new DaosObjectId(random.nextInt(), lowSeq.incrementAndGet());
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      int dataSize = 30;
      byte[] bytes = generateDataArray(dataSize);
      list.add(createEntryForUpdate("akey1", 10, 0, dataSize, bytes));
      IODataDesc desc = object.createDataDescForUpdate("dkey1", list);
      object.update(desc);
      List<IODataDesc.Entry> list2 = new ArrayList<>();
      dataSize = 60;
      byte[] bytes2 = generateDataArray(dataSize);
      list2.add(createEntryForUpdate("akey2", 10, 0, dataSize, bytes2));
      IODataDesc desc2 = object.createDataDescForUpdate("dkey2", list2);
      object.update(desc2);
      // list dkeys
      IOKeyDesc keyDesc0 = object.createKD(null);
      List<String> keyList0 = object.listDkeys(keyDesc0);
      Assert.assertEquals(2, keyList0.size());
      Assert.assertTrue(keyList0.contains("dkey1"));
      Assert.assertTrue(keyList0.contains("dkey2"));
      // punch dkey1
      object.punchDkeys(Arrays.asList("dkey1"));
      // list dkey2
      IOKeyDesc keyDesc = object.createKD(null);
      List<String> keyList1 = object.listDkeys(keyDesc);
      Assert.assertEquals(1, keyList1.size());
      Assert.assertEquals("dkey2", keyList1.get(0));
      // punch dkey2
      object.punchDkeys(Arrays.asList("dkey2"));
      keyDesc = object.createKD(null);
      List<String> keyList2 = object.listDkeys(keyDesc);
      Assert.assertEquals(0, keyList2.size());
    } finally {
      object.punch();
      object.close();
    }
  }

  @Test
  public void testListDkeysMultipletimes() throws Exception {
    DaosObjectId id = new DaosObjectId(random.nextInt(), lowSeq.incrementAndGet());
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      int dataSize = 10;
      int nbrOfKeys = (int)(Constants.KEY_LIST_BATCH_SIZE_DEFAULT*1.5);
      byte[] bytes = generateDataArray(dataSize);
      for (int i = 0; i < nbrOfKeys; i++) {
        list.add(createEntryForUpdate("akey" + i, 10, 0, dataSize, bytes));
        IODataDesc desc = object.createDataDescForUpdate("dkey" + i, list);
        object.update(desc);
        list.clear();
      }
      // list dkeys, reach number limit
      IOKeyDesc keyDesc = object.createKD(null);
      List<String> keys = object.listDkeys(keyDesc);
      Assert.assertEquals(Constants.KEY_LIST_BATCH_SIZE_DEFAULT, keys.size());
      ByteBuffer anchorBuffer = keyDesc.getAnchorBuffer();
      anchorBuffer.position(0);
      Assert.assertEquals(Constants.KEY_LIST_CODE_REACH_LIMIT, anchorBuffer.get());
      assertKeyValue(keys.get(1), "dkey", 200);
      assertKeyValue(keys.get(10), "dkey", 200);
      assertKeyValue(keys.get(Constants.KEY_LIST_BATCH_SIZE_DEFAULT - 1), "dkey", 200);
      // continue to list rest of them
      keyDesc.continueList();
      keys = object.listDkeys(keyDesc);
      int remaining = nbrOfKeys - Constants.KEY_LIST_BATCH_SIZE_DEFAULT;
      Assert.assertEquals(remaining, keys.size());
      assertKeyValue(keys.get(remaining - 3), "dkey", 200);
      assertKeyValue(keys.get(remaining - 2), "dkey", 200);
      assertKeyValue(keys.get(remaining - 1), "dkey", 200);
      anchorBuffer.position(0);
      Assert.assertEquals(Constants.KEY_LIST_CODE_ANCHOR_END, anchorBuffer.get());
    } finally {
      object.punch();
      object.close();
    }
  }

  private String varyKey(int idx) {
    StringBuilder sb = new StringBuilder();
    sb.append(idx);
    for (int i = 0; i < idx; i++) {
      sb.append(0);
    }
    return sb.toString();
  }

  @Test
  public void testListDkeysTooBig() throws Exception {
    DaosObjectId id = new DaosObjectId(random.nextInt(), lowSeq.incrementAndGet());
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      int dataSize = 10;
      int nbrOfKeys = 5;
      byte[] bytes = generateDataArray(dataSize);
      for (int i = 0; i < nbrOfKeys; i++) {
        list.add(createEntryForUpdate("akey" + varyKey(i), 10, 0, dataSize, bytes));
        IODataDesc desc = object.createDataDescForUpdate("dkey" + varyKey(i), list);
        object.update(desc);
        list.clear();
      }
      // list dkeys, small size
      IOKeyDesc keyDesc = object.createKDWithAllParams(null, 1, 2, 1);
      Exception ee = null;
      try {
        object.listDkeys(keyDesc);
      } catch (Exception e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      Assert.assertEquals(7, keyDesc.getSuggestedKeyLen());
      ByteBuffer anchorBuffer = keyDesc.getAnchorBuffer();
      anchorBuffer.position(0);
      Assert.assertEquals(Constants.KEY_LIST_CODE_KEY2BIG, anchorBuffer.get());
      // cannot continueList after key2big
      ee = null;
      try {
        keyDesc.continueList();
      } catch (Exception e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      // list 1 key
      IOKeyDesc keyDesc2 = object.createKDWithAllParams(null, 1,
        keyDesc.getSuggestedKeyLen(), 1);
      List<String> keys2 = object.listDkeys(keyDesc2);
      Assert.assertEquals(1, keys2.size());
      Assert.assertTrue(keys2.get(0).startsWith("dkey"));
      ByteBuffer anchorBuffer2 = keyDesc2.getAnchorBuffer();
      anchorBuffer2.position(0);
      Assert.assertEquals(Constants.KEY_LIST_CODE_REACH_LIMIT, anchorBuffer2.get());
      // list 3 keys
      IOKeyDesc keyDesc3 = object.createKDWithAllParams(null, 1,
        keyDesc.getSuggestedKeyLen(), 1);
      List<String> keys3;
      ByteBuffer anchorBuffer3;
      int size = 0;
      while (!keyDesc3.reachEnd()) {
        keyDesc3.continueList();
        keys3 = object.listDkeys(keyDesc3);
        Assert.assertEquals(1, keys3.size());
        Assert.assertTrue(keys3.get(0).startsWith("dkey"));
        anchorBuffer3 = keyDesc3.getAnchorBuffer();
        anchorBuffer3.position(0);
        size += 1;
      }
      Assert.assertEquals(nbrOfKeys, size);
    } finally {
      object.punch();
      object.close();
    }
  }

  private void assertKeyValue(String key, String prefix, int seq) {
    Assert.assertTrue(key.startsWith(prefix));
    Assert.assertTrue(Integer.valueOf(key.substring(prefix.length())) < seq);
  }

  @Test
  public void testPunchAkeys() throws IOException {
    DaosObjectId id = new DaosObjectId(random.nextInt(), lowSeq.incrementAndGet());
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      int dataSize = 30;
      byte[] bytes = generateDataArray(dataSize);
      list.add(createEntryForUpdate("akey1", 10, 0, 30, bytes));
      IODataDesc desc = object.createDataDescForUpdate("dkey1", list);
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
    DaosObjectId id = new DaosObjectId(random.nextInt(), lowSeq.incrementAndGet());
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      int dataSize = 30;
      byte[] bytes = generateDataArray(dataSize);
      list.add(createEntryForUpdate("akey1", 10, 0, dataSize, bytes));
      IODataDesc desc = object.createDataDescForUpdate("dkey1", list);
      object.update(desc);
      List<IODataDesc.Entry> list2 = new ArrayList<>();
      list2.add(createEntryForUpdate("akey1", 10, 0, dataSize, bytes));
      IODataDesc desc2 = object.createDataDescForUpdate("dkey2", list2);
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
    DaosObjectId id = new DaosObjectId(random.nextInt(), lowSeq.incrementAndGet());
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      int dataSize = 30;
      byte[] bytes = generateDataArray(dataSize);
      list.add(createEntryForUpdate("akey1", 10, 0, dataSize, bytes));
      IODataDesc desc = object.createDataDescForUpdate("dkey1", list);
      object.update(desc);
      List<IODataDesc.Entry> list2 = new ArrayList<>();
      list2.add(createEntryForUpdate("akey2", 10, 0, dataSize, bytes));
      IODataDesc desc2 = object.createDataDescForUpdate("dkey1", list2);
      object.update(desc2);
      // list akeys
      Thread.sleep(1000);
      IOKeyDesc keyDesc = object.createKD("dkey2");
      object.listAkeys(keyDesc);
    } finally {
      object.punch();
      object.close();
    }
  }

  private IODataDesc.Entry createEntryForFetch(String akey, int recordSize, int offset, int dataSize)
    throws IOException{
    return IODataDesc.createEntryForFetch(akey, IODataDesc.IodType.ARRAY, offset, recordSize, dataSize);
  }

  private IODataDesc.Entry createEntryForUpdate(String akey, int recordSize, int offset, int dataSize,
                                       byte[] bytes) throws IOException{
    ByteBuffer buffer = BufferAllocator.directBuffer(dataSize);
    buffer.put(bytes);
    buffer.flip();
    IODataDesc.Entry entry = IODataDesc.createEntryForUpdate(akey, IODataDesc.IodType.ARRAY, offset,
      recordSize, buffer);
    return entry;
  }

  private byte[] generateDataArray(int dataSize) {
    byte[] bytes = new byte[dataSize];
    for (int i = 0; i < dataSize; i++) {
      bytes[i] = (byte)((i + 33) % 128);
    }
    return bytes;
  }

  @AfterClass
  public static void afterClass() throws IOException {
    if (client != null) {
      client.close();
    }
  }
}
