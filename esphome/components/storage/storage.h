#pragma once

#include "esphome/core/helpers.h"

#include <cstdint>
#include <utility>

namespace esphome::storage {

/** A filesystem mounted into the VFS at a fixed path.
 *
 * While it is mounted, files below the mount point are used with the standard C file functions (fopen, opendir,
 * stat, unlink, ...). An implementation mounts and unmounts it and reports each change with set_mounted_().
 *
 * Threads and blocking:
 * - Implementations mount and unmount only from the main loop, so the mount callbacks always run there.
 * - File functions block until the medium answers: milliseconds for a card write, up to the network timeout for a
 *   network share. Long transfers belong in a task of their own, not in loop().
 * - The callback with false runs before the filesystem is removed, even when the medium has already gone. Every user
 *   closes its files and directories in that callback, and a user that reads or writes files from another task makes
 *   that task stop before the callback returns. Files left open become invalid.
 */
class Storage {
 public:
  explicit Storage(const char *mount_point) : mount_point_(mount_point) {}

  /// The path the filesystem is mounted at, such as "/sdcard".
  const char *get_mount_point() const { return this->mount_point_; }
  bool is_mounted() const { return this->mounted_; }
  /// True when files can be read but not created, changed or deleted. Always false while not mounted.
  bool is_read_only() const { return this->read_only_; }

  /// Fill in the size and free space in bytes. Returns false when not mounted or when the filesystem can not be read.
  virtual bool get_space(uint64_t &total_bytes, uint64_t &free_bytes) = 0;

  /// Called with true after the filesystem is mounted and with false before it is unmounted.
  template<typename F> void add_on_mount_callback(F &&callback) {
    this->mount_callback_.add(std::forward<F>(callback));
  }

 protected:
  /// Report a mount (true) or an unmount (false). read_only describes the filesystem that was mounted.
  void set_mounted_(bool mounted, bool read_only = false) {
    if (this->mounted_ == mounted)
      return;
    this->mounted_ = mounted;
    this->read_only_ = mounted && read_only;
    this->mount_callback_.call(mounted);
  }

  const char *const mount_point_;

 private:
  // Only set_mounted_() changes these, so read_only_ is never true while unmounted
  bool mounted_{false};
  bool read_only_{false};
  LazyCallbackManager<void(bool)> mount_callback_;
};

}  // namespace esphome::storage
