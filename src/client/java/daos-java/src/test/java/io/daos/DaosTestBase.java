package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "52475e08-8b05-4b03-8045-0af803f2abc0";
  public static final String DEFAULT_CONT_ID = "f5058e73-9bb1-4609-a32b-636c5a5d9072";

  public static final String DEFAULT_OBJECT_CONT_ID = "f5058e73-9bb1-4609-a32b-636c5a5d9072";

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
