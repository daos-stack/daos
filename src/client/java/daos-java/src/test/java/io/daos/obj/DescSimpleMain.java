package io.daos.obj;

import io.daos.DaosClient;
import io.netty.buffer.ByteBuf;

import java.io.*;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

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

  private static void generateData(DaosObject object, long totalBytes, int maps, int reduces) throws IOException {
    int akeyValLen = (int)(totalBytes/maps/reduces);
//    ByteBuf buf = BufferAllocator.objBufWithNativeOrder(akeyValLen);
    IODataDescSimple desc = object.createSimpleDataDesc(3, 4, 1,
        akeyValLen, true);

    populate(desc.getEntry(0).getDataBuffer());
    System.out.println("block size: " + akeyValLen);
    long start = System.nanoTime();
    try {
      for (int i = 0; i < reduces; i++) {
        desc.setDkey(padZero(i, 3));
        for (int j = 0; j < maps; j++) {
          IODataDescSimple.SimpleEntry entry = desc.getEntry(0);
          ByteBuf buf = entry.reuseBuffer();
          buf.writerIndex(akeyValLen);
          entry.setKeyForUpdate(padZero(j, 4), 0, buf);
          object.updateSimple(desc);
          desc.reuse();
        }
      }
    } finally {
      desc.release();
    }
    long expected = 1L*akeyValLen*reduces*maps;

    float seconds = ((float)(System.nanoTime()-start))/1000000000;
    System.out.println("perf (MB/s): " + ((float)expected)/seconds/1024/1024);
    System.out.println("total read (MB): " + expected/1024/1024);
    System.out.println("seconds: " + seconds);
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
                           int sizeLimit, int offset, int nbrOfDkeys) throws IOException {
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
    long start = System.nanoTime();
    IODataDescSimple desc = object.createSimpleDataDesc(3, 4, nbrOfEntries,
        akeyValLen, false);;
//    IODataDesc.Entry entry = desc.getEntry(0);
    try {
      for (int i = offset; i < end; i++) {
        desc.setDkey(padZero(i, 3));
        for (int j = 0; j < maps; j++) {
          desc.getEntry(idx++).setKeyForFetch(padZero(j, 4), 0, akeyValLen);
          if (idx == nbrOfEntries) {
            // read
            object.fetchSimple(desc);
            for (int k = 0; k < nbrOfEntries; k++) {
              totalRead += desc.getEntry(k).getActualSize();
            }
            idx = 0;
            desc.reuse();
          }
        }
        if (idx > 0) {
          // read
          object.fetchSimple(desc);
          for (int k = 0; k < idx; k++) {
            totalRead += desc.getEntry(k).getActualSize();
          }
          idx = 0;
          desc.reuse();
        }
      }
    } finally {
      desc.release();
    }
    long expected = 1L*akeyValLen*nbrOfDkeys*maps;
    if (totalRead != expected) {
      throw new IOException("expect totalRead: " + expected + ", actual: " + totalRead);
    }

    float seconds = ((float)(System.nanoTime()-start))/1000000000;
    System.out.println("perf (MB/s): " + ((float)totalRead)/seconds/1024/1024);
    System.out.println("total read (MB): " + totalRead/1024/1024);
    System.out.println("seconds: " + seconds);
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
