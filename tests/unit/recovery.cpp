#include <cstdio>
#include <experimental/filesystem>

#include "gflags/gflags.h"
#include "gtest/gtest.h"

#include "communication/bolt/v1/decoder/decoder.hpp"
#include "database/dbms.hpp"
#include "durability/file_reader_buffer.hpp"
#include "durability/recovery.hpp"
#include "utils/assert.hpp"

DECLARE_int32(snapshot_cycle_sec);

namespace fs = std::experimental::filesystem;

char tmp[] = "XXXXXX";
const fs::path SNAPSHOTS_RECOVERY_ALL_DB_DIR = mkdtemp(tmp);
const fs::path SNAPSHOTS_RECOVERY_DEFAULT_DB_DIR =
    SNAPSHOTS_RECOVERY_ALL_DB_DIR / "default";

std::vector<fs::path> GetFilesFromDir(
    const std::string &snapshots_default_db_dir) {
  std::vector<fs::path> files;
  for (auto &file : fs::directory_iterator(snapshots_default_db_dir))
    files.push_back(file.path());
  return files;
}

void CleanDbDir() {
  if (!fs::exists(SNAPSHOTS_RECOVERY_DEFAULT_DB_DIR)) return;
  std::vector<fs::path> files =
      GetFilesFromDir(SNAPSHOTS_RECOVERY_DEFAULT_DB_DIR);
  for (auto file : files) {
    fs::remove(file);
  }
}

class RecoveryTest : public ::testing::Test {
 protected:
  void TearDown() override { CleanDbDir(); }

  void SetUp() override {
    CleanDbDir();
    FLAGS_snapshot_cycle_sec = -1;
  }
  const int max_retained_snapshots_ = 10;
};

void CreateSmallGraph(Dbms &dbms) {
  auto dba = dbms.active();

  // setup (v1) - [:likes] -> (v2) <- [:hates] - (v3)
  auto va1 = dba->InsertVertex();
  auto va2 = dba->InsertVertex();
  dba->InsertEdge(va1, va2, dba->EdgeType("likes"));
  auto va3 = dba->InsertVertex();
  dba->InsertEdge(va3, va2, dba->EdgeType("hates"));
  dba->Commit();
}

void CreateBigGraph(Dbms &dbms) {
  // creates graph with one inner vertex connected with other 999 outer vertices
  // relationships are directed from outer vertices to the inner vertex
  // every vertex hash label "label" and property "prop" with value "prop"
  // every relationship has type "type" and property "prop" with value "prop"
  auto dba = dbms.active();
  auto va_middle = dba->InsertVertex();
  va_middle.add_label(dba->Label("label"));
  va_middle.PropsSet(dba->Property("prop"), "prop");
  for (int i = 1; i < 1000; ++i) {
    auto va = dba->InsertVertex();
    va.add_label(dba->Label("label"));
    va.PropsSet(dba->Property("prop"), "prop");
    auto ea = dba->InsertEdge(va, va_middle, dba->EdgeType("type"));
    ea.PropsSet(dba->Property("prop"), "prop");
  }
  dba->Commit();
}

void TakeSnapshot(Dbms &dbms, int max_retained_snapshots_) {
  auto dba = dbms.active();
  Snapshooter snapshooter;
  snapshooter.MakeSnapshot(*dba.get(), SNAPSHOTS_RECOVERY_DEFAULT_DB_DIR,
                           max_retained_snapshots_);
}

std::string GetLatestSnapshot() {
  std::vector<fs::path> files =
      GetFilesFromDir(SNAPSHOTS_RECOVERY_DEFAULT_DB_DIR);
  permanent_assert(static_cast<int>(files.size()) == 1,
                   "No snapshot files in folder.");
  std::sort(files.rbegin(), files.rend());
  return files[0];
}

TEST_F(RecoveryTest, TestEncoding) {
  // Creates snapshot of the small graph. Uses file_reader_buffer and bolt
  // decoder to read data from the snapshot and reads graph from it. After
  // reading graph is tested.
  Dbms dbms;
  CreateSmallGraph(dbms);
  TakeSnapshot(dbms, max_retained_snapshots_);
  std::string snapshot = GetLatestSnapshot();

  FileReaderBuffer buffer;
  communication::bolt::Decoder<FileReaderBuffer> decoder(buffer);

  snapshot::Summary summary;
  buffer.Open(snapshot, summary);

  communication::bolt::DecodedValue dv;
  decoder.ReadValue(&dv);

  std::vector<int64_t> ids;
  std::vector<std::string> edge_types;
  for (int i = 0; i < summary.vertex_num_; ++i) {
    communication::bolt::DecodedValue vertex_dv;
    decoder.ReadValue(&vertex_dv);
    auto &vertex = vertex_dv.ValueVertex();
    ids.push_back(vertex.id);
  }
  std::vector<int64_t> from, to;
  for (int i = 0; i < summary.edge_num_; ++i) {
    communication::bolt::DecodedValue edge_dv;
    decoder.ReadValue(&edge_dv);
    auto &edge = edge_dv.ValueEdge();
    from.push_back(edge.from);
    to.push_back(edge.to);
    edge_types.push_back(edge.type);
  }
  buffer.Close();

  ASSERT_EQ(to.size(), 2U);
  ASSERT_EQ(from.size(), 2U);
  EXPECT_EQ(buffer.hash(), summary.hash_);
  EXPECT_NE(edge_types.end(),
            std::find(edge_types.begin(), edge_types.end(), "hates"));
  EXPECT_NE(edge_types.end(),
            std::find(edge_types.begin(), edge_types.end(), "likes"));
  EXPECT_EQ(to[0], to[1]);
  EXPECT_NE(from[0], from[1]);
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), to[0]));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), from[0]));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), from[1]));
}

TEST_F(RecoveryTest, TestEncodingAndDecoding) {
  // Creates snapshot of the small graph. Uses Recovery to recover graph from
  // the snapshot file. After creation graph is tested.
  Dbms dbms;
  CreateSmallGraph(dbms);
  TakeSnapshot(dbms, max_retained_snapshots_);
  std::string snapshot = GetLatestSnapshot();

  // New dbms is needed - old dbms has database "default"
  Dbms dbms_recover;
  auto dba_recover = dbms_recover.active();

  Recovery recovery;
  EXPECT_TRUE(recovery.Recover(snapshot, *dba_recover));

  std::vector<VertexAccessor> vertices;
  std::vector<EdgeAccessor> edges;

  auto dba = dbms_recover.active();
  int64_t vertex_count = 0;
  for (const auto &vertex : dba->Vertices(false)) {
    vertices.push_back(vertex);
    vertex_count++;
  }
  EXPECT_EQ(vertex_count, 3);

  int64_t edge_count = 0;
  for (const auto &edge : dba->Edges(false)) {
    EXPECT_NE(vertices.end(),
              std::find(vertices.begin(), vertices.end(), edge.to()));
    EXPECT_NE(vertices.end(),
              std::find(vertices.begin(), vertices.end(), edge.from()));
    edges.push_back(edge);
    edge_count++;
  }
  permanent_assert(static_cast<int>(edges.size()) == 2,
                   "There should be two edges.");

  EXPECT_EQ(edge_count, 2);
  EXPECT_TRUE(edges[0].to() == edges[1].to());
  EXPECT_FALSE(edges[0].from() == edges[1].from());
}

TEST_F(RecoveryTest, TestEncodingAndRecovering) {
  // Creates snapshot of the big graph. Uses Recovery to recover graph from
  // the snapshot file. After creation graph is tested.
  Dbms dbms;
  CreateBigGraph(dbms);
  TakeSnapshot(dbms, max_retained_snapshots_);
  std::string snapshot = GetLatestSnapshot();

  // New dbms is needed - old dbms has database "default"
  Dbms dbms_recover;
  auto dba_recover = dbms_recover.active();

  Recovery recovery;
  EXPECT_TRUE(recovery.Recover(snapshot, *dba_recover));

  auto dba_get = dbms_recover.active();
  int64_t vertex_count = 0;
  for (const auto &vertex : dba_get->Vertices(false)) {
    EXPECT_EQ(vertex.labels().size(), 1);
    EXPECT_TRUE(vertex.has_label(dba_get->Label("label")));
    query::TypedValue prop =
        query::TypedValue(vertex.PropsAt(dba_get->Property("prop")));
    query::TypedValue expected_prop = query::TypedValue(PropertyValue("prop"));
    EXPECT_TRUE((prop == expected_prop).Value<bool>());
    vertex_count++;
  }
  EXPECT_EQ(vertex_count, 1000);

  int64_t edge_count = 0;
  for (const auto &edge : dba_get->Edges(false)) {
    EXPECT_EQ(edge.EdgeType(), dba_get->EdgeType("type"));
    query::TypedValue prop =
        query::TypedValue(edge.PropsAt(dba_get->Property("prop")));
    query::TypedValue expected_prop = query::TypedValue(PropertyValue("prop"));
    EXPECT_TRUE((prop == expected_prop).Value<bool>());
    edge_count++;
  }
  EXPECT_EQ(edge_count, 999);
  dba_get->Commit();
}

TEST_F(RecoveryTest, TestLabelPropertyIndexRecovery) {
  // Creates snapshot of the graph with indices.
  Dbms dbms;
  auto dba = dbms.active();
  dba->BuildIndex(dba->Label("label"), dba->Property("prop"));
  dba->Commit();
  CreateBigGraph(dbms);
  TakeSnapshot(dbms, max_retained_snapshots_);
  std::string snapshot = GetLatestSnapshot();

  Dbms dbms_recover;
  auto dba_recover = dbms_recover.active();

  Recovery recovery;
  EXPECT_TRUE(recovery.Recover(snapshot, *dba_recover));

  auto dba_get = dbms_recover.active();
  EXPECT_EQ(dba_get->GetIndicesKeys().size(), 1);
  EXPECT_TRUE(dba_get->LabelPropertyIndexExists(dba_get->Label("label"),
                                                dba_get->Property("prop")));

  int64_t vertex_count = 0;
  for (const auto &vertex : dba_get->Vertices(false)) {
    EXPECT_EQ(vertex.labels().size(), 1);
    EXPECT_TRUE(vertex.has_label(dba_get->Label("label")));
    query::TypedValue prop =
        query::TypedValue(vertex.PropsAt(dba_get->Property("prop")));
    query::TypedValue expected_prop = query::TypedValue(PropertyValue("prop"));
    EXPECT_TRUE((prop == expected_prop).Value<bool>());
    vertex_count++;
  }
  EXPECT_EQ(vertex_count, 1000);

  int64_t edge_count = 0;
  for (const auto &edge : dba_get->Edges(false)) {
    EXPECT_EQ(edge.EdgeType(), dba_get->EdgeType("type"));
    query::TypedValue prop =
        query::TypedValue(edge.PropsAt(dba_get->Property("prop")));
    query::TypedValue expected_prop = query::TypedValue(PropertyValue("prop"));
    EXPECT_TRUE((prop == expected_prop).Value<bool>());
    edge_count++;
  }
  EXPECT_EQ(edge_count, 999);
  dba_get->Commit();
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
