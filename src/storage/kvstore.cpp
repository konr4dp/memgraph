#include <rocksdb/db.h>
#include <rocksdb/options.h>

#include "storage/kvstore.hpp"
#include "utils/file.hpp"

namespace storage {

struct KVStore::impl {
  std::experimental::filesystem::path storage;
  std::unique_ptr<rocksdb::DB> db;
  rocksdb::Options options;
};

KVStore::KVStore(fs::path storage) : pimpl_(std::make_unique<impl>()) {
  pimpl_->storage = storage;
  if (!utils::EnsureDir(pimpl_->storage))
    throw KVStoreError("Folder for the key-value store " +
                       pimpl_->storage.string() + " couldn't be initialized!");
  pimpl_->options.create_if_missing = true;
  rocksdb::DB *db = nullptr;
  auto s = rocksdb::DB::Open(pimpl_->options, storage.c_str(), &db);
  if (!s.ok())
    throw KVStoreError("RocksDB couldn't be initialized inside " +
                       storage.string() + " -- " + std::string(s.ToString()));
  pimpl_->db.reset(db);
}

KVStore::~KVStore() {}

KVStore::KVStore(KVStore &&other) { pimpl_ = std::move(other.pimpl_); }

KVStore &KVStore::operator=(KVStore &&other) {
  pimpl_ = std::move(other.pimpl_);
  return *this;
}

bool KVStore::Put(const std::string &key, const std::string &value) {
  auto s = pimpl_->db->Put(rocksdb::WriteOptions(), key, value);
  return s.ok();
}

std::experimental::optional<std::string> KVStore::Get(
    const std::string &key) const noexcept {
  std::string value;
  auto s = pimpl_->db->Get(rocksdb::ReadOptions(), key, &value);
  if (!s.ok()) return std::experimental::nullopt;
  return value;
}

bool KVStore::Delete(const std::string &key) {
  auto s = pimpl_->db->Delete(rocksdb::WriteOptions(), key);
  return s.ok();
}

bool KVStore::DeletePrefix(const std::string &prefix) {
  std::unique_ptr<rocksdb::Iterator> iter = std::unique_ptr<rocksdb::Iterator>(
      pimpl_->db->NewIterator(rocksdb::ReadOptions()));
  for (iter->Seek(prefix); iter->Valid() && iter->key().starts_with(prefix);
       iter->Next()) {
    if (!pimpl_->db->Delete(rocksdb::WriteOptions(), iter->key()).ok())
      return false;
  }
  return true;
}

// iterator

struct KVStore::iterator::impl {
  const KVStore *kvstore;
  std::string prefix;
  std::unique_ptr<rocksdb::Iterator> it;
  std::pair<std::string, std::string> disk_prop;
};

KVStore::iterator::iterator(const KVStore *kvstore, const std::string &prefix,
                            bool at_end)
    : pimpl_(std::make_unique<impl>()) {
  pimpl_->kvstore = kvstore;
  pimpl_->prefix = prefix;
  pimpl_->it = std::unique_ptr<rocksdb::Iterator>(
      pimpl_->kvstore->pimpl_->db->NewIterator(rocksdb::ReadOptions()));
  pimpl_->it->Seek(pimpl_->prefix);
  if (!pimpl_->it->Valid() || !pimpl_->it->key().starts_with(pimpl_->prefix) ||
      at_end)
    pimpl_->it = nullptr;
}

KVStore::iterator::iterator(KVStore::iterator &&other) {
  pimpl_ = std::move(other.pimpl_);
}

KVStore::iterator::~iterator() {}

KVStore::iterator &KVStore::iterator::operator=(KVStore::iterator &&other) {
  pimpl_ = std::move(other.pimpl_);
  return *this;
}

KVStore::iterator &KVStore::iterator::operator++() {
  pimpl_->it->Next();
  if (!pimpl_->it->Valid() || !pimpl_->it->key().starts_with(pimpl_->prefix))
    pimpl_->it = nullptr;
  return *this;
}

bool KVStore::iterator::operator==(const iterator &other) const {
  return pimpl_->kvstore == other.pimpl_->kvstore &&
         pimpl_->prefix == other.pimpl_->prefix &&
         pimpl_->it == other.pimpl_->it;
}

bool KVStore::iterator::operator!=(const iterator &other) const {
  return !(*this == other);
}

KVStore::iterator::reference KVStore::iterator::operator*() {
  pimpl_->disk_prop = {pimpl_->it->key().ToString(),
                       pimpl_->it->value().ToString()};
  return pimpl_->disk_prop;
}

KVStore::iterator::pointer KVStore::iterator::operator->() { return &**this; }

void KVStore::iterator::SetInvalid() { pimpl_->it = nullptr; }

bool KVStore::iterator::IsValid() { return pimpl_->it != nullptr; }

// TODO(ipaljak) The complexity of the size function should be at most
//               logarithmic.
size_t KVStore::Size(const std::string &prefix) {
  size_t size = 0;
  for (auto it = this->begin(prefix); it != this->end(prefix); ++it) ++size;
  return size;
}

}  // namespace storage
