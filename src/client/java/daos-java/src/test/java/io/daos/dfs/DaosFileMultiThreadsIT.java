package io.daos.dfs;

import io.daos.Constants;
import io.daos.DaosIOException;
import io.daos.DaosObjectType;
import io.daos.DaosTestBase;
import org.junit.AfterClass;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

public class DaosFileMultiThreadsIT {

  private static String poolId;
  private static String contId;

  private static DaosFsClient client;

  @BeforeClass
  public static void setup() throws Exception {
    poolId = DaosTestBase.getPoolId();
    contId = DaosTestBase.getContId();

    client = DaosFsClientTestBase.prepareFs(poolId, contId);
  }

  private void operate(String path, Op op, int threadNum) throws Exception {
    List<CreateThread> list = new ArrayList<>();
    int num = threadNum;
    for (int i = 0; i < num; i++) {
      list.add(new CreateThread(client, path, op));
    }
    for (int i = 0; i < num; i++) {
      list.get(i).start();
    }

    for (int i = 0; i < num; i++) {
      list.get(i).join();
    }

    for (int i = 0; i < num; i++) {
      Assert.assertFalse(list.get(i).failed);
    }

    DaosFile file = client.getFile(path);
    boolean ex = file.exists();
    file.release();
    Assert.assertTrue(ex);
  }

  /**
   * TODO: to be resumed after DAOS team adding "conditional update" to fix concurrency issue
   *
   * @throws Exception
   */
  public void testCreateNewFile() throws Exception {
    operate("/zjf/xyz/abc/def/test", Op.CREATE, 50);
  }

  /**
   * TODO: to be resumed after DAOS team adding "conditional update" to fix concurrency issue
   *
   * @throws Exception
   */
  public void testMkdir() throws Exception {
    String path = "/zjf2/xyz/abc/def/dir";
    operate(path, Op.MKDIR, 50);
  }

  @Test
  public void testExists() throws Exception {
    String path = "/zjf3/Terasort/Input2/part-m-00045";
    DaosFile file = client.getFile(path);
    file.createNewFile(0611, DaosObjectType.OC_SX, Constants.FILE_DEFAULT_CHUNK_SIZE, true);
    StringBuilder sb = new StringBuilder();
    String str = "abcdeffffffffffffffffffffffffdddddddddddddddddddddddd";
    int size = 0;
    while (size < 2 * 1024 * 1024) {
      sb.append(str);
      size += str.length();
    }
    String data = sb.toString();
    ByteBuffer buffer = ByteBuffer.allocateDirect(size);

    for (int i = 0; i < 24; i++) {
      buffer.put(data.getBytes());
      file.write(buffer, 0, 0, data.length());
      buffer.clear();
    }
    file.release();

    operate(path, Op.EXISTS, 50);
    file.release();
  }

  @AfterClass
  public static void teardown() throws Exception {
    if (client != null) {
      client.close();
    }
  }

  enum Op {
    CREATE, MKDIR, EXISTS
  }


  static class CreateThread extends Thread {
    DaosFsClient client;
    String path;
    boolean failed;
    Op op;

    CreateThread(DaosFsClient client, String path, Op op) throws Exception {
      this.client = client;
      this.path = path;
//      this.client = DaosFsClientTestBase.prepareFs(poolId, contId);
      this.op = op;
    }

    CreateThread(DaosFsClient client, String path) throws Exception {
      this(client, path, Op.CREATE);
    }

    @Override
    public void run() {
      DaosFile file = client.getFile(path);
      try {
        switch (op) {
          case CREATE:
            file.createNewFile(true);
            break;
          case MKDIR:
            file.mkdirs();
            break;
          case EXISTS:
            Assert.assertTrue(file.exists());
            DaosFile file2 = client.getFile(path);
            file2.getStatAttributes();
            file2.release();
            break;
        }
        file.release();
      } catch (Exception e) {
        if (!(e instanceof DaosIOException)) {
          e.printStackTrace();
          failed = true;
        } else {
          DaosIOException de = (DaosIOException) e;
          switch (op) {
            case CREATE:
            case MKDIR:
              if (de.getErrorCode() != Constants.ERROR_CODE_FILE_EXIST) {
                e.printStackTrace();
                failed = true;
              }
              break;
            case EXISTS:
              e.printStackTrace();
              failed = true;
          }
        }
      }
    }
  }
}
