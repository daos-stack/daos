package io.daos.obj;

import io.daos.DaosClient;
import io.netty.buffer.ByteBuf;
import org.junit.Assert;

import java.io.*;
import java.util.*;

public class DescSimpleMain {

  private static final String OUTPUT_PATH = "./output";
  private static final String FILE_NAME_DKEY = "dkeys.txt";
  private static final String FILE_NAME_AKEY = "akeys.txt";

  private static final long DEFAULT_TOTAL_BYTES = 210L*1024*1024*1024;
  private static final int DEFAULT_NUMBER_OF_MAPS = 1000;
  private static final int DEFAULT_NUMBER_OF_REDUCES = 125;

  private static final StringBuilder sb = new StringBuilder();

  public static void main(String[] args) throws Exception {
    String poolId = args[0];
    String containerId = args[1];
    String svc = args[2];
    long oid = Long.valueOf(args[3]);
    String op = args[4];
    long totalBytes = Long.valueOf(args[5]);
    if (totalBytes <= 0) {
      totalBytes = DEFAULT_TOTAL_BYTES;
    }
    int maps = Integer.valueOf(args[6]);
    if (maps <= 0) {
      maps = DEFAULT_NUMBER_OF_MAPS;
    }
    int reduces = Integer.valueOf(args[7]);
    if (reduces <= 0) {
      reduces = DEFAULT_NUMBER_OF_REDUCES;
    }

    DaosObjClient objClient = null;
    DaosObject object = null;
    try {
      objClient = getObjClient(poolId, containerId, svc);
      object = openObject(objClient, oid);
      if ("generate".equalsIgnoreCase(op)) {
        generateData(object, totalBytes, maps, reduces);
      } else if ("read".equalsIgnoreCase(op)) {
        int sizeLimit = 2 * 1024 * 1024;
        int nbrOfDkeys = reduces;
        int offset = 0;
        if (args.length >= 9) {
          sizeLimit = Integer.valueOf(args[8]);
        }
        if (args.length >= 11) {
          offset = Integer.valueOf(args[9]);
          nbrOfDkeys = Integer.valueOf(args[10]);
        }
        read(object, totalBytes, maps, reduces, sizeLimit, offset, nbrOfDkeys);
      }
    } finally {
      if (objClient != null) {
        objClient.close();
      }
      if (object != null) {
        object.close();
      }
      DaosClient.FINALIZER.run();
    }
  }

  private static byte[] generateDataArray(int dataSize) {
    byte[] bytes = new byte[dataSize];
    for (int i = 0; i < dataSize; i++) {
      bytes[i] = (byte) ((i + 33) % 128);
    }
    return bytes;
  }

  private static void generateData(DaosObject object, long totalBytes, int maps, int reduces) throws IOException, InterruptedException {
    int akeyValLen = (int)(totalBytes/maps/reduces);
//    ByteBuf buf = BufferAllocator.objBufWithNativeOrder(akeyValLen);
//    IOSimpleDataDesc desc = object.createSimpleDataDesc(3, 1, akeyValLen,
//         null);
    System.out.println("block size: " + akeyValLen);
    byte[] data = generateDataArray(akeyValLen);
    DaosEventQueue dq = DaosEventQueue.getInstance(128, 4, 1, akeyValLen);
    for (int i = 0; i < dq.getNbrOfEvents(); i++) {
      DaosEventQueue.Event e = dq.getEvent(i);
      IOSimpleDataDesc.SimpleEntry entry = e.getDesc().getEntry(0);
      ByteBuf buf = entry.reuseBuffer();
      buf.writeBytes(data);
    }
    DaosEventQueue.Event e;
    IOSimpleDataDesc desc;
    IOSimpleDataDesc.SimpleEntry entry;
    ByteBuf buf;
    long start = System.nanoTime();
    try {
      for (int i = 0; i < reduces; i++) {
        for (int j = 0; j < maps; j++) {
          e = dq.acquireEventBlock(true, 1000, null);
          desc = e.reuseDesc();
          desc.setDkey(String.valueOf(i));
          entry = desc.getEntry(0);
          buf = entry.reuseBuffer();
          buf.writerIndex(buf.capacity());
          entry.setEntryForUpdate(String.valueOf(j), 0, buf);
          object.updateSimple(desc);
        }
      }
      dq.waitForCompletion(10000, null);
      float seconds = ((float)(System.nanoTime()-start))/1000000000;
      long expected = 1L*akeyValLen*reduces*maps;
      System.out.println("perf (MB/s): " + ((float)expected)/seconds/1024/1024);
      System.out.println("total read (MB): " + expected/1024/1024);
      System.out.println("seconds: " + seconds);
    } finally {
      DaosEventQueue.destroyAll();
    }
  }

  private static String padZero(int v, int len) {
    sb.setLength(0);
    sb.append(v);
    int gap = len - sb.length();
    if (gap > 0) {
      for (int i = 0; i < gap; i++) {
        sb.append(0);
      }
    }
    return sb.toString();
  }

  private static void read(DaosObject object, long totalBytes, int maps, int reduces,
                           int sizeLimit, int offset, int nbrOfDkeys) throws IOException, InterruptedException {
//    Map<String, Integer> dkeyMap = readKeys(FILE_NAME_DKEY, offset, nbrOfDkeys);
//    Map<String, Integer> akeyMap = readKeys(FILE_NAME_AKEY, 0, -1);
    int akeyValLen = (int)(totalBytes/maps/reduces);
    System.out.println("block size: " + akeyValLen);
    long totalRead = 0;
    if (offset < 0 || offset >= reduces) {
      throw new IOException("offset should be no less than 0 and less than reduces: " + reduces +", offset: " + offset);
    }
    if (nbrOfDkeys <= 0) {
      throw new IOException("number of dkeys should be more than 0, " + nbrOfDkeys);
    }
    int end = nbrOfDkeys + offset;
    if (end > reduces) {
      throw new IOException("offset + nbrOfDkeys should not exceed reduces. " + (nbrOfDkeys + offset) + " > " + reduces);
    }
    int nbrOfEntries = sizeLimit/akeyValLen;
    int idx = 0;
    List<IOSimpleDataDesc> compList = new LinkedList<>();
    DaosEventQueue dq = DaosEventQueue.getInstance(128, 4, nbrOfEntries, akeyValLen);
//    IODataDesc.Entry entry = desc.getEntry(0);
    DaosEventQueue.Event e;
    IOSimpleDataDesc desc;
    long start = System.nanoTime();
    try {
      // acquire and check
      compList.clear();
      e = dq.acquireEventBlock(false, 1000, compList);
      desc = e.reuseDesc();

      for (int i = offset; i < end; i++) {
        for (int j = 0; j < maps; j++) {
          desc.getEntry(idx++).setEntryForFetch(String.valueOf(j), 0, akeyValLen);
          if (idx == nbrOfEntries) {
            // read
            desc.setDkey(String.valueOf(i));
            object.fetchSimple(desc);
            idx = 0;
            // acquire and check
            compList.clear();
            e = dq.acquireEventBlock(false, 1000, compList);
            for (IOSimpleDataDesc d : compList) {
              for (int k = 0; k < nbrOfEntries; k++) {
                totalRead += d.getEntry(k).getActualSize();
              }
            }
            desc = e.reuseDesc();
          }
        }
        if (idx > 0) {
          // read
          object.fetchSimple(desc);
          idx = 0;
          // acquire and check
          compList.clear();
          e = dq.acquireEventBlock(false, 1000, compList);
          for (IOSimpleDataDesc d : compList) {
            for (int k = 0; k < idx; k++) {
              totalRead += d.getEntry(k).getActualSize();
            }
          }
          desc = e.reuseDesc();
        }
      }

      dq.waitForCompletion(10000, compList);
      float seconds = ((float)(System.nanoTime()-start))/1000000000;

      long expected = 1L*akeyValLen*nbrOfDkeys*maps;
      if (totalRead != expected) {
        throw new IOException("expect totalRead: " + expected + ", actual: " + totalRead);
      }
      System.out.println("perf (MB/s): " + ((float)totalRead)/seconds/1024/1024);
      System.out.println("total read (MB): " + totalRead/1024/1024);
      System.out.println("seconds: " + seconds);
    } finally {
      DaosEventQueue.destroyAll();
    }
  }

  private static IODataDesc.Entry createEntry(String akey, long offset, long readSize) throws IOException {
    return IODataDesc.createEntryForFetch(akey, IODataDesc.IodType.ARRAY, 1, (int)offset,
        (int)readSize);
  }

  private static Map<String, Integer> readKeys(String filename, int offset, int nbrOfKeys) throws IOException {
    Map<String, Integer> map = new LinkedHashMap<>();
    File f = new File(OUTPUT_PATH, filename);
    int index = 0;
    int count = 0;
    try (BufferedReader reader = new BufferedReader(new FileReader(f))) {
      String line;
      while ((line=reader.readLine()) != null) {
        if (!line.isEmpty()) {
          if (index++ >= offset) {
            String[] keyAndLen = line.split("=");
            map.put(keyAndLen[0], keyAndLen.length > 1 ? Integer.valueOf(keyAndLen[1]) : 0);
            if (nbrOfKeys > 0 && (++count) >= nbrOfKeys) {
              break;
            }
          }
        }
      }
    }
    if (nbrOfKeys > 0 && nbrOfKeys != map.size()) {
      throw new IOException("expect number of keys: " + nbrOfKeys + ", actual: " + map.size());
    }
    return map;
  }

  private static void populate(ByteBuf buf) {
    for (int i = 0; i < buf.capacity(); i++) {
      int b = i % 128;
      if (b < 32) {
        b += 32;
      }
      buf.writeByte(b);
    }
  }

  private static List<String> listAkeys(DaosObject object, String dkey) throws IOException {
    IOKeyDesc keyDesc = null;
    List<String> list = new ArrayList<>();
    try {
      keyDesc = object.createKD(dkey);
      while (!keyDesc.reachEnd()) {
        keyDesc.continueList();
        list.addAll(object.listAkeys(keyDesc));
      }
      return list;
    } finally {
      if (keyDesc != null) {
        keyDesc.release();
      }
    }
  }

  private static void output(List<String> list, String filename) throws IOException {
    File dir = new File(OUTPUT_PATH);
    if (!dir.exists()) {
      dir.mkdir();
    }
    File file = new File(dir, filename);
    if (file.exists()) {
      File backup = new File(file.getAbsolutePath() + "." + System.currentTimeMillis());
      file.renameTo(backup);
    }
    try (FileOutputStream fos = new FileOutputStream(file)) {
      for (String key : list) {
        fos.write(key.getBytes());
        fos.write('\n');
      }
    }
  }

  private static List<String> listDkeies(DaosObject object) throws IOException {
    IOKeyDesc keyDesc = null;
    List<String> list = new ArrayList<>();
    try {
      keyDesc = object.createKD(null);
      while (!keyDesc.reachEnd()) {
        keyDesc.continueList();
        list.addAll(object.listDkeys(keyDesc));
      }
      return list;
    } finally {
      if (keyDesc != null) {
        keyDesc.release();
      }
    }
  }

  private static DaosObject openObject(DaosObjClient objClient, long high)
    throws Exception {
    DaosObjectId oid = new DaosObjectId(high, 0L);
    oid.encode();
    DaosObject object = objClient.getObject(oid);
    object.open();
    return object;
  }

  private static DaosObjClient getObjClient(String pid, String cid, String svc) throws IOException {
    DaosObjClient objClient = new DaosObjClient.DaosObjClientBuilder()
        .poolId(pid).containerId(cid).ranks(svc)
        .build();
    return objClient;
  }
}
