#include "storage/kvstore.hpp"

#include "glog/logging.h"
#include "utils/file.hpp"

namespace storage {

struct KVStore::impl {};

KVStore::KVStore(fs::path storage) {}

KVStore::~KVStore() {}

bool KVStore::Put(const std::string &key, const std::string &value) {
  CHECK(false)
      << "Unsupported operation (KVStore::Put) -- this is a dummy kvstore";
}

std::experimental::optional<std::string> KVStore::Get(
    const std::string &key) const noexcept {
  CHECK(false)
      << "Unsupported operation (KVStore::Get) -- this is a dummy kvstore";
}

bool KVStore::Delete(const std::string &key) {
  CHECK(false)
      << "Unsupported operation (KVStore::Delete) -- this is a dummy kvstore";
}

bool KVStore::DeletePrefix(const std::string &prefix) {
  CHECK(false) << "Unsupported operation (KVStore::DeletePrefix) -- this is a "
                  "dummy kvstore";
}

// iterator

struct KVStore::iterator::impl {};

KVStore::iterator::iterator(const KVStore *kvstore, const std::string &prefix,
                            bool at_end)
    : pimpl_(new impl()) {}

KVStore::iterator::iterator(KVStore::iterator &&other) {
  pimpl_ = std::move(other.pimpl_);
}

KVStore::iterator::~iterator() {}

KVStore::iterator &KVStore::iterator::operator=(KVStore::iterator &&other) {
  pimpl_ = std::move(other.pimpl_);
  return *this;
}

KVStore::iterator &KVStore::iterator::operator++() {
  CHECK(false) << "Unsupported operation (&KVStore::iterator::operator++) -- "
                  "this is a dummy kvstore";
}

bool KVStore::iterator::operator==(const iterator &other) const { return true; }

bool KVStore::iterator::operator!=(const iterator &other) const {
  return false;
}

KVStore::iterator::reference KVStore::iterator::operator*() {
  CHECK(false) << "Unsupported operation (KVStore::iterator::operator*)-- this "
                  "is a dummy kvstore";
}

KVStore::iterator::pointer KVStore::iterator::operator->() {
  CHECK(false) << "Unsupported operation (KVStore::iterator::operator->) -- "
                  "this is a dummy kvstore";
}

void KVStore::iterator::SetInvalid() {}

bool KVStore::iterator::IsValid() { return false; }

size_t KVStore::Size(const std::string &prefix) { return 0; }

}  // namespace storage
