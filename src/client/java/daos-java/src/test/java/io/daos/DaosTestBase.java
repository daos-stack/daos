package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "40098eea-01df-41a9-972b-8971e575c19a";
  public static final String DEFAULT_CONT_ID = "6f083217-a918-404e-88ed-83df8d5b5c95";

  public static final String DEFAULT_POOL_LABEL = "pool1";
  public static final String DEFAULT_CONT_LABEL = "cont1";

  public static final String DEFAULT_OBJECT_CONT_ID = "ceea91d5-e29a-4bae-bb7f-4fef8db60aa7";

  public static String getPoolId() {
    return System.getProperty("pool_id", DaosTestBase.DEFAULT_POOL_ID);
  }

  public static String getContId() {
    return System.getProperty("cont_id", DaosTestBase.DEFAULT_CONT_ID);
  }

  public static String getPoolLabel() {
    return System.getProperty("pool_label", DaosTestBase.DEFAULT_POOL_LABEL);
  }

  public static String getContLabel() {
    return System.getProperty("cont_label", DaosTestBase.DEFAULT_CONT_LABEL);
  }

  public static String getObjectContId() {
    return System.getProperty("object_cont_id", DaosTestBase.DEFAULT_OBJECT_CONT_ID);
  }
}
