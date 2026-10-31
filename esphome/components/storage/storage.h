#pragma once

#include "esphome/core/helpers.h"

#include <cstdint>

namespace esphome::storage {

/** A filesystem mounted into the VFS at a fixed path.
 *
 * While it is mounted, files below the mount point are used with the standard C file functions (fopen, opendir,
 * stat, unlink, ...). An implementation mounts and unmounts it and reports each change with set_mounted_().
 */
class Storage {
 public:
  explicit Storage(const char *mount_point) : mount_point_(mount_point) {}

  /// The path the filesystem is mounted at, such as "/sdcard".
  const char *get_mount_point() const { return this->mount_point_; }
  bool is_mounted() const { return this->mounted_; }

  /// Fill in the size and free space in bytes. Returns false when not mounted or when the filesystem can not be read.
  virtual bool get_space(uint64_t &total_bytes, uint64_t &free_bytes) = 0;

  /// Called with true after the filesystem is mounted and with false after it is unmounted.
  template<typename F> void add_on_mount_callback(F &&callback) {
    this->mount_callback_.add(std::forward<F>(callback));
  }

 protected:
  void set_mounted_(bool mounted) {
    if (this->mounted_ == mounted)
      return;
    this->mounted_ = mounted;
    this->mount_callback_.call(mounted);
  }

  const char *mount_point_;
  bool mounted_{false};
  LazyCallbackManager<void(bool)> mount_callback_;
};

}  // namespace esphome::storage
