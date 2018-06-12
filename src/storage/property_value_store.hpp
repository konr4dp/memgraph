#pragma once

#include <atomic>
#include <experimental/optional>
#include <string>
#include <vector>

#include "storage/kvstore.hpp"
#include "storage/property_value.hpp"
#include "storage/types.hpp"

const std::string disk_key_separator = "_";

std::string DiskKey(const std::string &version_key,
                    const std::string &property_id);

std::string DiskKeyPrefix(const std::string &version_key);

/**
 * A collection of properties accessed in a map-like way using a key of type
 * Storage::Property.
 *
 * PropertyValueStore handles storage on disk or in memory. Property key defines
 * where the corresponding property should be stored. Each instance of
 * PropertyValueStore contains a version_key_ member which specifies where on
 * disk should the properties be stored. That key is inferred from a static
 * global counter global_key_cnt_.
 *
 * The underlying implementation of in-memory storage is not necessarily
 * std::map.
 */
class PropertyValueStore {
  using Property = storage::Property;
  using Location = storage::Location;

 public:
  // Property name which will be used to store vertex/edge ids inside property
  // value store
  static constexpr char IdPropertyName[] = "__id__";

  PropertyValueStore() = default;
  PropertyValueStore(const PropertyValueStore &old);

  ~PropertyValueStore();

  /**
   * Returns a PropertyValue (by reference) at the given key.
   * If the key does not exist, the Null property is returned.
   *
   * This is NOT thread-safe, the reference might not be valid
   * when used in a multithreaded scenario.
   *
   * @param key The key for which a PropertyValue is sought.
   * @return  See above.
   */
  PropertyValue at(const Property &key) const;

  /**
   * Set overriding for character constants. Forces conversion
   * to std::string, otherwise templating might cast the pointer
   * to something else (bool) and mess things up.
   */
  void set(const Property &key, const char *value);

  /**
   * Set overriding for PropertyValue. When setting a Null value it
   * calls 'erase' instead of inserting the Null into storage.
   */
  void set(const Property &key, const PropertyValue &value);

  /**
   * Removes the PropertyValue for the given key.
   *
   * @param key - The key for which to remove the property.
   *
   * @return true if the operation was successful and there is nothing stored
   *         under given key after this operation.
   */
  bool erase(const Property &key);

  /** Removes all the properties (both in-mem and on-disk) from this store. */
  void clear();

  /**
   * Returns a static storage::kvstore instance used for storing properties on
   * disk. This hack is needed due to statics that are internal to rocksdb and
   * availability of durability_directory flag.
   */
  storage::KVStore &DiskStorage() const;

  /**
   * Custom PVS iterator behaves as if all properties are stored in a single
   * iterable collection of std::pair<Property, PropertyValue>.
   */
  class iterator final
      : public std::iterator<
            std::input_iterator_tag,                     // iterator_category
            std::pair<Property, PropertyValue>,          // value_type
            long,                                        // difference_type
            const std::pair<Property, PropertyValue> *,  // pointer
            const std::pair<Property, PropertyValue> &   // reference
            > {
   public:
    iterator() = delete;

    iterator(const PropertyValueStore *PVS,
             std::vector<std::pair<Property, PropertyValue>>::const_iterator
                 memory_it,
             storage::KVStore::iterator disk_it)
        : PVS_(PVS), memory_it_(memory_it), disk_it_(std::move(disk_it)) {}

    iterator(const iterator &other) = delete;

    iterator(iterator &&other) = default;

    iterator &operator=(iterator &&other) = default;

    iterator &operator=(const iterator &other) = delete;

    iterator &operator++() {
      if (memory_it_ != PVS_->props_.end())
        ++memory_it_;
      else
        ++disk_it_;
      return *this;
    }

    bool operator==(const iterator &other) const {
      return PVS_ == other.PVS_ && memory_it_ == other.memory_it_ &&
             disk_it_ == other.disk_it_;
    }

    bool operator!=(const iterator &other) const { return !(*this == other); }

    reference operator*() {
      if (memory_it_ != PVS_->props_.end()) return *memory_it_;
      std::pair<std::string, std::string> kv = *disk_it_;
      std::string prop_id =
          kv.first.substr(kv.first.find(disk_key_separator) + 1);
      disk_prop_ = {Property(std::stoi(prop_id), Location::Disk),
                    PVS_->DeserializeProp(kv.second)};
      return disk_prop_.value();
    }

    pointer operator->() { return &**this; }

   private:
    const PropertyValueStore *PVS_;
    std::vector<std::pair<Property, PropertyValue>>::const_iterator memory_it_;
    storage::KVStore::iterator disk_it_;
    std::experimental::optional<std::pair<Property, PropertyValue>> disk_prop_;
  };

  size_t size() const {
    return props_.size() +
           DiskStorage().Size(DiskKeyPrefix(std::to_string(version_key_)));
  }

  iterator begin() const {
    return iterator(
        this, props_.begin(),
        DiskStorage().begin(DiskKeyPrefix(std::to_string(version_key_))));
  }

  iterator end() const {
    return iterator(
        this, props_.end(),
        DiskStorage().end(DiskKeyPrefix(std::to_string(version_key_))));
  }

 private:
  static std::atomic<uint64_t> global_key_cnt_;
  uint64_t version_key_ = global_key_cnt_++;

  std::vector<std::pair<Property, PropertyValue>> props_;

  /**
   * Serializes a single PropertyValue into std::string.
   *
   * @param prop - Property to be serialized.
   *
   * @return Serialized property.
   */
  std::string SerializeProp(const PropertyValue &prop) const;

  /**
   * Deserializes a single PropertyValue from std::string.
   *
   * @param serialized_prop - Serialized property.
   *
   * @return Deserialized property.
   */
  PropertyValue DeserializeProp(const std::string &serialized_prop) const;

  storage::KVStore ConstructDiskStorage() const;
};

