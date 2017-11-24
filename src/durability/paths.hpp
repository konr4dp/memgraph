#pragma once

#include <experimental/filesystem>
#include <experimental/optional>
#include <string>

#include "glog/logging.h"

#include "transactions/type.hpp"
#include "utils/datetime/timestamp.hpp"
#include "utils/string.hpp"

namespace durability {
const std::string kSnapshotDir = "snapshots";
const std::string kWalDir = "wal";

/// Ensures the given durability directory exists and is ready for use. Creates
/// the directory if it doesn't exist.
inline void CheckDurabilityDir(const std::string &durability_dir) {
  namespace fs = std::experimental::filesystem;
  if (fs::exists(durability_dir)) {
    CHECK(fs::is_directory(durability_dir)) << "The durability directory path '"
                                            << durability_dir
                                            << "' is not a directory!";
  } else {
    bool success = fs::create_directory(durability_dir);
    CHECK(success) << "Failed to create durability directory '"
                   << durability_dir << "'.";
  }
}

/// Returns the transaction id contained in the file name. If the filename is
/// not a parseable WAL file name, nullopt is returned. If the filename
/// represents the "current" WAL file, then the maximum possible transaction ID
/// is returned because that's appropriate for the recovery logic (the current
/// WAL does not yet have a maximum transaction ID and can't be discarded by
/// the recovery regardless of the snapshot from which the transaction starts).
inline std::experimental::optional<tx::transaction_id_t>
TransactionIdFromWalFilename(const std::string &name) {
  auto nullopt = std::experimental::nullopt;
  // Get the max_transaction_id from the file name that has format
  // "XXXXX__max_transaction_<MAX_TRANS_ID>"
  auto file_name_split = utils::RSplit(name, "__", 1);
  if (file_name_split.size() != 2) {
    LOG(WARNING) << "Unable to parse WAL file name: " << name;
    return nullopt;
  }
  if (file_name_split[1] == "current")
    return std::numeric_limits<tx::transaction_id_t>::max();
  file_name_split = utils::RSplit(file_name_split[1], "_", 1);
  if (file_name_split.size() != 2) {
    LOG(WARNING) << "Unable to parse WAL file name: " << name;
    return nullopt;
  }
  auto &tx_id_str = file_name_split[1];
  try {
    return std::stoll(tx_id_str);
  } catch (std::invalid_argument &) {
    LOG(WARNING) << "Unable to parse WAL file name tx ID: " << tx_id_str;
    return nullopt;
  } catch (std::out_of_range &) {
    LOG(WARNING) << "WAL file name tx ID too large: " << tx_id_str;
    return nullopt;
  }
}

/// Generates a file path for a write-ahead log file. If given a transaction ID
/// the file name will contain it. Otherwise the file path is for the "current"
/// WAL file for which the max tx id is still unknown.
inline auto WalFilenameForTransactionId(
    const std::experimental::filesystem::path &wal_dir,
    std::experimental::optional<tx::transaction_id_t> tx_id =
        std::experimental::nullopt) {
  auto file_name = Timestamp::now().to_iso8601();
  if (tx_id) {
    file_name += "__max_transaction_" + std::to_string(*tx_id);
  } else {
    file_name += "__current";
  }
  return wal_dir / file_name;
}
}
