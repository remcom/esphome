#include <gtest/gtest.h>

#include <vector>

#include "esphome/components/storage/storage.h"

namespace esphome::storage::testing {

class FakeStorage : public Storage {
 public:
  FakeStorage() : Storage("/fake") {}
  bool get_space(uint64_t & /*total_bytes*/, uint64_t & /*free_bytes*/) override { return false; }
  void report(bool mounted, bool read_only = false) { this->set_mounted_(mounted, read_only); }
};

TEST(Storage, StartsUnmountedAndWritable) {
  FakeStorage storage;
  EXPECT_STREQ(storage.get_mount_point(), "/fake");
  EXPECT_FALSE(storage.is_mounted());
  EXPECT_FALSE(storage.is_read_only());
}

TEST(Storage, CallbackRunsOncePerChange) {
  FakeStorage storage;
  std::vector<bool> calls;
  storage.add_on_mount_callback([&calls](bool mounted) { calls.push_back(mounted); });

  storage.report(true);
  storage.report(true);
  storage.report(false);
  storage.report(false);

  EXPECT_EQ(calls, (std::vector<bool>{true, false}));
}

TEST(Storage, StateIsUpdatedBeforeTheCallback) {
  FakeStorage storage;
  bool mounted_in_callback = false;
  bool read_only_in_callback = false;
  storage.add_on_mount_callback([&](bool) {
    mounted_in_callback = storage.is_mounted();
    read_only_in_callback = storage.is_read_only();
  });

  storage.report(true, true);
  EXPECT_TRUE(mounted_in_callback);
  EXPECT_TRUE(read_only_in_callback);

  storage.report(false);
  EXPECT_FALSE(mounted_in_callback);
  EXPECT_FALSE(read_only_in_callback);
}

TEST(Storage, ReadOnlyIsClearedOnUnmount) {
  FakeStorage storage;
  storage.report(true, true);
  EXPECT_TRUE(storage.is_read_only());
  storage.report(false, true);
  EXPECT_FALSE(storage.is_mounted());
  EXPECT_FALSE(storage.is_read_only());
}

TEST(Storage, WorksWithoutCallbacks) {
  FakeStorage storage;
  storage.report(true);
  EXPECT_TRUE(storage.is_mounted());
  EXPECT_FALSE(storage.is_read_only());
}

}  // namespace esphome::storage::testing
