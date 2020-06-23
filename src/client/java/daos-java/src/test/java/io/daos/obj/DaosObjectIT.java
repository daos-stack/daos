package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.Constants;
import io.daos.DaosIOException;
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
  public void testObjectUpdateWithDifferentRecordSize() throws IOException {
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
      IODataDesc desc = object.createDataDescForUpdate("dkey1", list);
      object.update(desc);
      // update with different record size
      dataSize = 40;
      bytes = generateDataArray(dataSize);
      list.clear();
      list.add(createEntryForUpdate("akey1", 20, 0, dataSize, bytes));
      desc = object.createDataDescForUpdate("dkey1", list);
      Exception ee = null;
      try {
        object.update(desc);
      } catch (Exception e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      Assert.assertTrue(ee instanceof DaosIOException);
      DaosIOException de = (DaosIOException)ee;
      Assert.assertEquals(Constants.ERROR_CODE_ILLEGAL_ARG, de.getErrorCode());
      // succeed on different key
      dataSize = 40;
      bytes = generateDataArray(dataSize);
      list.clear();
      list.add(createEntryForUpdate("akey2", 20, 0, dataSize, bytes));
      desc = object.createDataDescForUpdate("dkey1", list);
      object.update(desc);
    } finally {
      if (object.isOpen()) {
        object.punch();
      }
      object.close();
    }
  }

  @Test
  public void testObjectUpdateWithExistingEntry() throws IOException {
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
      IODataDesc desc = object.createDataDescForUpdate("dkey1", list);
      object.update(desc);
      // update with different record size
      dataSize = 40;
      bytes = generateDataArray(dataSize);
      // old entry is in list
      list.add(createEntryForUpdate("akey2", 20, 0, dataSize, bytes));
      desc = object.createDataDescForUpdate("dkey1", list);
      Exception ee = null;
      try {
        object.update(desc);
      } catch (Exception e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      Assert.assertTrue(ee instanceof IllegalArgumentException);
      Assert.assertTrue(ee.getMessage().contains("global buffer is set already"));
    } finally {
      if (object.isOpen()) {
        object.punch();
      }
      object.close();
    }
  }

  @Test
  public void testObjectFetchSimple() throws IOException {
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
      IODataDesc.Entry entry = createEntryForFetch("akey1", 10, 0, 80);
      list2.add(entry);
      IODataDesc desc2 = object.createDataDescForFetch("dkey1", list2);
      object.fetch(desc2);
      Assert.assertEquals(dataSize, entry.getActualSize());
      byte[] actualBytes = new byte[dataSize];
      entry.get(actualBytes);
      Assert.assertTrue(Arrays.equals(bytes, actualBytes));
      // fetch from offset
      list2.clear();
      entry = createEntryForFetch("akey2", 10, 10, 80);
      list2.add(entry);
      desc2 = object.createDataDescForFetch("dkey1", list2);
      object.fetch(desc2);
      Assert.assertEquals(dataSize - 10, entry.getActualSize());
      byte[] actualBytes2 = new byte[dataSize - 10];
      entry.get(actualBytes2);
      byte[] originBytes = Arrays.copyOfRange(bytes, 10, 30);
      Assert.assertTrue(Arrays.equals(originBytes, actualBytes2));
    } finally {
      if (object.isOpen()) {
        object.punch();
      }
      object.close();
    }
  }

  @Test
  public void testUpdateAndFetchSingleType() throws Exception {
    DaosObjectId id = new DaosObjectId(random.nextInt(), lowSeq.incrementAndGet());
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      Assert.assertTrue(object.isOpen());
      List<IODataDesc.Entry> list = new ArrayList<>();
      int dataSize = 10;
      byte[] bytes = generateDataArray(dataSize);
      list.add(createEntryForUpdateWithTypeOfSingle("akey1", 10, 0, dataSize, bytes));
      list.add(createEntryForUpdateWithTypeOfSingle("akey2", 10, 0, dataSize, bytes));
      IODataDesc desc = object.createDataDescForUpdate("dkey1", list);
      object.update(desc);
      // fetch one akey
      List<IODataDesc.Entry> list2 = new ArrayList<>();
      IODataDesc.Entry entry = createEntryForFetchWithTypeOfSingle("akey2", 10, 0, 10);
      list2.add(entry);
      IODataDesc desc2 = object.createDataDescForFetch("dkey1", list2);
      object.fetch(desc2);
      Assert.assertEquals(10, entry.getActualSize());
      byte[] actualBytes = new byte[entry.getActualSize()];
      entry.get(actualBytes);
      Assert.assertTrue(Arrays.equals(bytes, actualBytes));
      // fetch two akeys
      list2.clear();
      list2.add(createEntryForFetchWithTypeOfSingle("akey2", 10, 0, 10));
      list2.add(createEntryForFetchWithTypeOfSingle("akey1", 10, 0, 10));
      desc2 = object.createDataDescForFetch("dkey1", list2);
      object.fetch(desc2);
      Assert.assertEquals(10, list2.get(0).getActualSize());
      Assert.assertEquals(10, list2.get(1).getActualSize());
      list2.get(0).get(actualBytes);
      Assert.assertTrue(Arrays.equals(bytes, actualBytes));
      list2.get(1).get(actualBytes);
      Assert.assertTrue(Arrays.equals(bytes, actualBytes));
    } finally {
      if (object.isOpen()) {
        object.punch();
      }
      object.close();
    }
  }

  @Test
  public void testObjectFetchWithIncorrectRecordSize() throws IOException {
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
      // fetch akey1, bigger record size
      List<IODataDesc.Entry> list2 = new ArrayList<>();
      IODataDesc.Entry entry = createEntryForFetch("akey1", 20, 0, 25);
      list2.add(entry);
      IODataDesc desc2 = object.createDataDescForFetch("dkey1", list2);
      object.fetch(desc2);
      Assert.assertEquals(dataSize, entry.getActualSize());
      byte[] actualBytes = new byte[dataSize];
      entry.get(actualBytes);
      Assert.assertTrue(Arrays.equals(bytes, actualBytes));
      // fetch akey2, smaller record size

    } finally {
      if (object.isOpen()) {
        object.punch();
      }
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
      if (object.isOpen()) {
        object.punch();
      }
      object.close();
    }
  }

  @Test
  public void testListDkeysSimple() throws IOException {
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
      List<String> keyList = object.listDkeys(keyDesc);
      Assert.assertEquals(2, keyList.size());
      Assert.assertTrue(keyDesc.reachEnd());
    } finally {
      if (object.isOpen()) {
        object.punch();
      }
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
      if (object.isOpen()) {
        object.punch();
      }
      object.close();
    }
  }

  private void listKeysMultipleTimes(String dkey) throws Exception {
    DaosObjectId id = new DaosObjectId(random.nextInt(), lowSeq.incrementAndGet());
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      int dataSize = 10;
      int nbrOfKeys = (int) (Constants.KEY_LIST_BATCH_SIZE_DEFAULT * 1.5);
      byte[] bytes = generateDataArray(dataSize);
      if (dkey == null) {
        for (int i = 0; i < nbrOfKeys; i++) {
          list.add(createEntryForUpdate("akey" + i, 10, 0, dataSize, bytes));
          IODataDesc desc = object.createDataDescForUpdate("dkey" + i, list);
          object.update(desc);
          list.clear();
        }
      } else {
        for (int i = 0; i < nbrOfKeys; i++) {
          list.add(createEntryForUpdate("akey" + i, 10, 0, dataSize, bytes));
        }
        IODataDesc desc = object.createDataDescForUpdate(dkey, list);
        object.update(desc);
        list.clear();
      }
      // list keys, reach number limit
      IOKeyDesc keyDesc = object.createKD(dkey);
      List<String> keys = dkey == null ? object.listDkeys(keyDesc) : object.listAkeys(keyDesc);
      Assert.assertEquals(Constants.KEY_LIST_BATCH_SIZE_DEFAULT, keys.size());
      ByteBuffer anchorBuffer = keyDesc.getAnchorBuffer();
      anchorBuffer.position(0);
      Assert.assertEquals(Constants.KEY_LIST_CODE_REACH_LIMIT, anchorBuffer.get());
      String prefix = dkey == null ? "dkey" : "akey";
      assertKeyValue(keys.get(1), prefix, nbrOfKeys);
      assertKeyValue(keys.get(10), prefix, nbrOfKeys);
      assertKeyValue(keys.get(Constants.KEY_LIST_BATCH_SIZE_DEFAULT - 1), prefix, 200);
      // continue to list rest of them
      keyDesc.continueList();
      keys = dkey == null ? object.listDkeys(keyDesc) : object.listAkeys(keyDesc);
      int remaining = nbrOfKeys - Constants.KEY_LIST_BATCH_SIZE_DEFAULT;
      Assert.assertEquals(remaining, keys.size());
      assertKeyValue(keys.get(remaining - 3), prefix, nbrOfKeys);
      assertKeyValue(keys.get(remaining - 2), prefix, nbrOfKeys);
      assertKeyValue(keys.get(remaining - 1), prefix, nbrOfKeys);
      anchorBuffer.position(0);
      Assert.assertEquals(Constants.KEY_LIST_CODE_ANCHOR_END, anchorBuffer.get());
    } finally {
      if (object.isOpen()) {
        object.punch();
      }
      object.close();
    }
  }

  @Test
  public void testListDkeysMultipletimes() throws Exception {
    listKeysMultipleTimes(null);
  }

  private String varyKey(int idx) {
    StringBuilder sb = new StringBuilder();
    sb.append(idx);
    for (int i = 0; i < idx; i++) {
      sb.append(0);
    }
    return sb.toString();
  }

  private void testListKeysTooBig(String dkey) throws Exception {
    DaosObjectId id = new DaosObjectId(random.nextInt(), lowSeq.incrementAndGet());
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      int dataSize = 10;
      int nbrOfKeys = 5;
      byte[] bytes = generateDataArray(dataSize);
      if (dkey == null) {
        for (int i = 0; i < nbrOfKeys; i++) {
          list.add(createEntryForUpdate("akey" + i, 10, 0, dataSize, bytes));
          IODataDesc desc = object.createDataDescForUpdate("dkey" + i, list);
          object.update(desc);
          list.clear();
        }
      } else {
        for (int i = 0; i < nbrOfKeys; i++) {
          list.add(createEntryForUpdate("akey" + i, 10, 0, dataSize, bytes));
        }
        IODataDesc desc = object.createDataDescForUpdate(dkey, list);
        object.update(desc);
        list.clear();
      }
      // list dkeys, small size
      IOKeyDesc keyDesc = object.createKDWithAllParams(dkey, 2, 1, 4);
      List<String> keys1 = dkey == null ? object.listDkeys(keyDesc) : object.listAkeys(keyDesc);
      Assert.assertEquals(0, keys1.size());
      Assert.assertEquals(6, keyDesc.getSuggestedKeyLen());
      ByteBuffer anchorBuffer = keyDesc.getAnchorBuffer();
      anchorBuffer.position(0);
      Assert.assertEquals(Constants.KEY_LIST_CODE_KEY2BIG, anchorBuffer.get());
      // continueList after key2big
      keyDesc.continueList();
      keys1 = dkey == null ? object.listDkeys(keyDesc) : object.listAkeys(keyDesc);
      Assert.assertEquals(2, keys1.size());
      anchorBuffer.position(0);
      Assert.assertEquals(Constants.KEY_LIST_CODE_REACH_LIMIT, anchorBuffer.get());

      // list 1 key
      IOKeyDesc keyDesc2 = object.createKDWithAllParams(dkey, 1,
        keyDesc.getSuggestedKeyLen(), 1);
      List<String> keys2 = dkey == null ? object.listDkeys(keyDesc2) : object.listAkeys(keyDesc2);
      Assert.assertEquals(1, keys2.size());
      String prefix = dkey == null ? "dkey" : "akey";
      Assert.assertTrue(keys2.get(0).startsWith(prefix));
      ByteBuffer anchorBuffer2 = keyDesc2.getAnchorBuffer();
      anchorBuffer2.position(0);
      Assert.assertEquals(Constants.KEY_LIST_CODE_REACH_LIMIT, anchorBuffer2.get());
      // list 3 keys
      IOKeyDesc keyDesc3 = object.createKDWithAllParams(dkey, 1,
        keyDesc.getSuggestedKeyLen(), 1);
      List<String> keys3;
      ByteBuffer anchorBuffer3;
      int size = 0;
      while (!keyDesc3.reachEnd()) {
        keyDesc3.continueList();
        keys3 = dkey == null ? object.listDkeys(keyDesc3) : object.listAkeys(keyDesc3);
        int s1 = keys3.size();
        size += s1;
        anchorBuffer3 = keyDesc3.getAnchorBuffer();
        anchorBuffer3.position(0);
        if (s1 == 0) {
          if (size < nbrOfKeys) {
            Assert.assertEquals(Constants.KEY_LIST_CODE_KEY2BIG, anchorBuffer3.get());
          }
        } else {
          Assert.assertEquals(Constants.KEY_LIST_CODE_REACH_LIMIT, anchorBuffer3.get());
          Assert.assertTrue(keys3.get(0).startsWith(prefix));
        }
      }
      Assert.assertEquals(nbrOfKeys, size);
    } finally {
      if (object.isOpen()) {
        object.punch();
      }
      object.close();
    }
  }

  @Test
  public void testListDkeysTooBig() throws Exception {
    testListKeysTooBig(null);
  }

  private void assertKeyValue(String key, String prefix, int seq) {
    Assert.assertTrue(key.startsWith(prefix));
    Assert.assertTrue(Integer.valueOf(key.substring(prefix.length())) < seq);
  }

  @Test
  public void testPunchAndListAkeys() throws IOException {
    DaosObjectId id = new DaosObjectId(random.nextInt(), lowSeq.incrementAndGet());
    id.encode();
    DaosObject object = client.getObject(id);
    try {
      object.open();
      List<IODataDesc.Entry> list = new ArrayList<>();
      int dataSize = 30;
      byte[] bytes = generateDataArray(dataSize);
      list.add(createEntryForUpdate("akey1", 10, 0, 30, bytes));
      list.add(createEntryForUpdate("akey2", 10, 0, 30, bytes));
      list.add(createEntryForUpdate("akey3", 10, 0, 30, bytes));
      IODataDesc desc = object.createDataDescForUpdate("dkey1", list);
      object.update(desc);
      // punch one akey
      object.punchAkeys("dkey1", Arrays.asList("akey1"));
      IOKeyDesc keyDesc = object.createKD("dkey1");
      List<String> keys = object.listAkeys(keyDesc);
      Assert.assertEquals(2, keys.size());
      // punch two akeys
      object.punchAkeys("dkey1", Arrays.asList("akey3", "akey2"));
      keyDesc = object.createKD("dkey1");
      keys = object.listAkeys(keyDesc);
      Assert.assertEquals(0, keys.size());
    } finally {
      if (object.isOpen()) {
        object.punch();
      }
      object.close();
    }
  }

  @Test
  public void testListAkeysSimple() throws Exception {
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
      // list akeys from non-existing dkey
      IOKeyDesc keyDesc = object.createKD("dkey2");
      List<String> keys = object.listAkeys(keyDesc);
      Assert.assertEquals(0, keys.size());
      Assert.assertTrue(keyDesc.reachEnd());
      // list akeys
      keyDesc = object.createKD("dkey1");
      keys = object.listAkeys(keyDesc);
      Assert.assertEquals(2, keys.size());
      Assert.assertTrue(keyDesc.reachEnd());
    } finally {
      if (object.isOpen()) {
        object.punch();
      }
      object.close();
    }
  }

  @Test
  public void testListAkeysMultipletimes() throws Exception {
    listKeysMultipleTimes("dkey2");
  }

  @Test
  public void testListAkeysTooBig() throws Exception {
    testListKeysTooBig("dkey3");
  }

  private IODataDesc.Entry createEntryForFetch(String akey, IODataDesc.IodType type, int recordSize,
                                               int offset, int dataSize) throws IOException {
    return IODataDesc.createEntryForFetch(akey, type, recordSize, offset, dataSize);
  }

  private IODataDesc.Entry createEntryForFetch(String akey, int recordSize, int offset, int dataSize)
    throws IOException {
    return createEntryForFetch(akey, IODataDesc.IodType.ARRAY, recordSize, offset, dataSize);
  }

  private IODataDesc.Entry createEntryForFetchWithTypeOfSingle(String akey, int recordSize, int offset, int dataSize)
    throws IOException {
    return createEntryForFetch(akey, IODataDesc.IodType.SINGLE, recordSize, offset, dataSize);
  }

  private IODataDesc.Entry createEntryForUpdate(String akey, IODataDesc.IodType type, int recordSize, int offset,
                                                int dataSize, byte[] bytes) throws IOException {
    ByteBuffer buffer = BufferAllocator.directBuffer(dataSize);
    buffer.put(bytes);
    buffer.flip();
    return IODataDesc.createEntryForUpdate(akey, type, recordSize, offset, buffer);
  }

  private IODataDesc.Entry createEntryForUpdateWithTypeOfSingle(String akey, int recordSize, int offset, int dataSize,
                                                byte[] bytes) throws IOException {
    return createEntryForUpdate(akey, IODataDesc.IodType.SINGLE, recordSize, offset, dataSize, bytes);
  }

  private IODataDesc.Entry createEntryForUpdate(String akey, int recordSize, int offset, int dataSize,
                                                byte[] bytes) throws IOException {
    return createEntryForUpdate(akey, IODataDesc.IodType.ARRAY, recordSize, offset, dataSize, bytes);
  }

  private byte[] generateDataArray(int dataSize) {
    byte[] bytes = new byte[dataSize];
    for (int i = 0; i < dataSize; i++) {
      bytes[i] = (byte) ((i + 33) % 128);
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
