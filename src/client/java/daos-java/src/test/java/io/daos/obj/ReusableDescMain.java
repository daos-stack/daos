package io.daos.obj;

import io.daos.DaosClient;
import io.netty.buffer.ByteBuf;

import java.io.*;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.*;

public class ReusableDescMain {

  private static final String OUTPUT_PATH = "./output";
  private static final String FILE_NAME_DKEY = "dkeys.txt";
  private static final String FILE_NAME_AKEY = "akeys.txt";

  private static final long DEFAULT_TOTAL_BYTES = 210L*1024*1024*1024;
  private static final int DEFAULT_NUMBER_OF_MAPS = 1000;
  private static final int DEFAULT_NUMBER_OF_REDUCES = 125;

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

    int threads = 1;
    if (args.length >= 12) {
      threads = Integer.valueOf(args[11]);
    }

    DaosObjClient objClient = null;
    DaosObject object = null;
    try {
      objClient = getObjClient(poolId, containerId, svc);
      object = openObject(objClient, oid);

      if (op.toLowerCase().startsWith("write")) {
        if (op.equalsIgnoreCase("write")) {
          write(object, totalBytes, maps, reduces, sizeLimit, offset, nbrOfDkeys, true);
        } else if (op.equalsIgnoreCase("write-threads")) {
          writeThreads(object, totalBytes, maps, reduces, sizeLimit, offset, nbrOfDkeys, threads);
        } else {
          System.out.println("unknown write operation: " + op);
        }
      } else if (op.toLowerCase().startsWith("read")) {
        if (op.equalsIgnoreCase("read")) {
          read(object, totalBytes, maps, reduces, sizeLimit, offset, nbrOfDkeys, true);
        } else if (op.equalsIgnoreCase("read-threads")) {
          readThreads(object, totalBytes, maps, reduces, sizeLimit, offset, nbrOfDkeys,
              threads);
        } else {
          System.out.println("unknown read operation: " + op);
        }
      } else {
        System.out.println("unknown operation: " + op);
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

  private static void readThreads(DaosObject object, long totalBytes, int maps, int reduces,
                                  int sizeLimit, int offset, int nbrOfDkeys, int threads) throws IOException, InterruptedException, ExecutionException {
    int ext = nbrOfDkeys/threads;
    if (nbrOfDkeys%threads != 0) {
      throw new IOException("nbrOfDkeys: " + nbrOfDkeys +", should be a multiple of threads " + threads);
    }
    ExecutorService exe = Executors.newFixedThreadPool(threads);
    List<ReadTask> tasks = new ArrayList<>();
    for (int i = offset; i < offset + nbrOfDkeys; i+=ext) {
      tasks.add(new ReadTask(object, totalBytes, maps, reduces, sizeLimit, i, ext));
    }
    float total = 0.0f;
    try {
      List<Future<Float>> rstList = new ArrayList<>();
      for (int i = 0; i < threads; i++) {
        rstList.add(exe.submit(tasks.get(i)));
      }

      for (Future<Float> f : rstList) {
        total += f.get();
      }

    } finally {
      System.out.println("perf: " + total);
      exe.shutdownNow();
      exe.awaitTermination(2, TimeUnit.SECONDS);
    }
  }

  static class ReadTask implements Callable<Float> {

    private DaosObject object;
    private long totalBytes;
    private final int maps;
    private final int reduces;
    private int sizeLimit;
    private final int offset;
    private final int nbrOfDkeys;

    public ReadTask(DaosObject object, long totalBytes, int maps, int reduces,
                    int sizeLimit, int offset, int nbrOfDkeys){
      this.object = object;
      this.totalBytes = totalBytes;
      this.maps = maps;
      this.reduces = reduces;

      this.sizeLimit = sizeLimit;
      this.offset = offset;
      this.nbrOfDkeys = nbrOfDkeys;
    }
    @Override
    public Float call() throws Exception {
      return read(object, totalBytes, maps, reduces, sizeLimit, offset, nbrOfDkeys, false);
    }
  }

  private static void writeThreads(DaosObject object, long totalBytes, int maps, int reduces,
                                   int sizeLimit, int offset, int nbrOfDkeys, int threads) throws Exception {
    int ext = nbrOfDkeys/threads;
    if (nbrOfDkeys%threads != 0) {
      throw new IOException("nbrOfDkeys: " + nbrOfDkeys +", should be a multiple of threads " + threads);
    }
    ExecutorService exe = Executors.newFixedThreadPool(threads);
    List<WriteTask> tasks = new ArrayList<>();
    for (int i = offset; i < offset + nbrOfDkeys; i+=ext) {
      tasks.add(new WriteTask(object, totalBytes, maps, reduces, sizeLimit, i, ext));
    }
    float total = 0.0f;
    try {
      List<Future<Float>> rstList = new ArrayList<>();
      for (int i = 0; i < threads; i++) {
        rstList.add(exe.submit(tasks.get(i)));
      }

      for (Future<Float> f : rstList) {
        total += f.get();
      }

    } finally {
      System.out.println("perf: " + total);
      exe.shutdownNow();
      exe.awaitTermination(2, TimeUnit.SECONDS);
    }
  }

  static class WriteTask implements Callable<Float> {

    private DaosObject object;
    private long totalBytes;
    private final int maps;
    private final int reduces;
    private int sizeLimit;
    private final int offset;
    private final int nbrOfDkeys;

    public WriteTask(DaosObject object, long totalBytes, int maps, int reduces,
                     int sizeLimit, int offset, int nbrOfDkeys){
      this.object = object;
      this.totalBytes = totalBytes;
      this.maps = maps;
      this.reduces = reduces;

      this.sizeLimit = sizeLimit;
      this.offset = offset;
      this.nbrOfDkeys = nbrOfDkeys;
    }
    @Override
    public Float call() throws Exception {
      return write(object, totalBytes, maps, reduces, sizeLimit, offset, nbrOfDkeys, false);
    }
  }

  private static float write(DaosObject object, long totalBytes, int maps, int reduces,
                            int sizeLimit, int offset, int nbrOfDkeys, boolean destroy) throws IOException {
    int akeyValLen = (int)(totalBytes/maps/reduces);
//    ByteBuf buf = BufferAllocator.objBufWithNativeOrder(akeyValLen);
    System.out.println("block size: " + akeyValLen);
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

    IODataDescSync desc = object.createReusableDesc(20, 1, akeyValLen,
        IODataDescSync.IodType.ARRAY, 1, true);
    populate(desc.getEntry(0).getDataBuffer());
    long start = System.nanoTime();
    try {
      for (int i = offset; i < end; i++) {
        for (int j = 0; j < maps; j++) {
          IODataDesc.Entry entry = desc.getEntry(0);
          ByteBuf buf = ((IODataDescSync.SyncEntry)entry).reuseBuffer();
          buf.writerIndex(akeyValLen);
          desc.setDkey(String.valueOf(i));
          ((IODataDescSync.SyncEntry)entry).setKey(String.valueOf(j), 0, buf);
          object.update(desc);
        }
      }
      long stop = System.nanoTime();
      float seconds = ((float)(stop-start))/1000000000;
      long expected = 1L*akeyValLen*nbrOfDkeys*maps;
      float rst = ((float)expected)/seconds/1024/1024;
      System.out.println("start: " + start);
      System.out.println("stop: " + stop);
      System.out.println("perf (MB/s): " + rst);
      System.out.println("total read (MB): " + expected/1024/1024);
      System.out.println("seconds: " + seconds);
      return rst;
    } finally {
      desc.release();
    }
  }

  private static float read(DaosObject object, long totalBytes, int maps, int reduces,
                           int sizeLimit, int offset, int nbrOfDkeys, boolean destroy) throws IOException {
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
    IODataDescSync desc = object.createReusableDesc(20, nbrOfEntries,
        akeyValLen,
        IODataDescSync.IodType.ARRAY, 1, false);
//    IODataDesc.Entry entry = desc.getEntry(0);
    try {
      for (int i = offset; i < end; i++) {
        for (int j = 0; j < maps; j++) {
          desc.setDkey(String.valueOf(i));
          ((IODataDescSync.SyncEntry)desc.getEntry(idx++)).setKey(String.valueOf(j), 0, akeyValLen);
          if (idx == nbrOfEntries) {
            // read
            object.fetch(desc);
            for (int k = 0; k < nbrOfEntries; k++) {
              totalRead += desc.getEntry(k).getActualSize();
            }
            idx = 0;
          }
        }
        if (idx > 0) {
          // read
          object.fetch(desc);
          for (int k = 0; k < idx; k++) {
            totalRead += desc.getEntry(k).getActualSize();
          }
          idx = 0;
        }
      }
    } finally {
      desc.release();
    }
    long expected = 1L*akeyValLen*nbrOfDkeys*maps;
    if (totalRead != expected) {
      throw new IOException("expect totalRead: " + expected + ", actual: " + totalRead);
    }
    long stop = System.nanoTime();
    float seconds = ((float)(stop-start))/1000000000;
    float rst = ((float)totalRead)/seconds/1024/1024;
    System.out.println("start: " + start);
    System.out.println("stop: " + stop);
    System.out.println("perf (MB/s): " + rst);
    System.out.println("total read (MB): " + totalRead/1024/1024);
    System.out.println("seconds: " + seconds);
    return rst;
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
    oid.encode(objClient.getContPtr());
    DaosObject object = objClient.getObject(oid);
    object.open();
    return object;
  }

  private static DaosObjClient getObjClient(String pid, String cid, String svc) throws IOException {
    DaosObjClient objClient = new DaosObjClient.DaosObjClientBuilder()
        .poolId(pid).containerId(cid)
        .build();
    return objClient;
  }
}
