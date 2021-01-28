package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "9ac718c7-9a4c-46d2-99c3-cd4095a7ba86";
  public static final String DEFAULT_CONT_ID = "7f2e9207-5957-41a0-bf68-8d13cda28fa7";

  public static final String DEFAULT_OBJECT_CONT_ID = "0ab1f374-25b8-4154-9e23-efeb94829c25";

  public static String getPoolId() {
    return System.getProperty("pool_id", DaosTestBase.DEFAULT_POOL_ID);
  }

  public static String getContId() {
    return System.getProperty("cont_id", DaosTestBase.DEFAULT_CONT_ID);
  }

  public static String getObjectContId() {
    return System.getProperty("object_cont_id", DaosTestBase.DEFAULT_OBJECT_CONT_ID);
  }
}
