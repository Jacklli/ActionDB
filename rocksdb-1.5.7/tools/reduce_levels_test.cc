// Copyright (c) 2012 Facebook. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "leveldb/db.h"
#include "db/db_impl.h"
#include "db/version_set.h"
#include "util/logging.h"
#include "util/testutil.h"
#include "util/testharness.h"
#include "util/ldb_cmd.h"

namespace leveldb {

class ReduceLevelTest {
public:
  ReduceLevelTest() {
    dbname_ = test::TmpDir() + "/db_reduce_levels_test";
    DestroyDB(dbname_, Options());
    db_ = NULL;
  }

  Status OpenDB(bool create_if_missing, int levels,
      int mem_table_compact_level);

  Status Put(const std::string& k, const std::string& v) {
    return db_->Put(WriteOptions(), k, v);
  }

  std::string Get(const std::string& k) {
    ReadOptions options;
    std::string result;
    Status s = db_->Get(options, k, &result);
    if (s.IsNotFound()) {
      result = "NOT_FOUND";
    } else if (!s.ok()) {
      result = s.ToString();
    }
    return result;
  }

  Status CompactMemTable() {
    if (db_ == NULL) {
      return Status::InvalidArgument("DB not opened.");
    }
    DBImpl* db_impl = reinterpret_cast<DBImpl*>(db_);
    return db_impl->TEST_CompactMemTable();
  }

  void CloseDB() {
    if (db_ != NULL) {
      delete db_;
      db_ = NULL;
    }
  }

  bool ReduceLevels(int target_level);

  int FilesOnLevel(int level) {
    std::string property;
    ASSERT_TRUE(
        db_->GetProperty("leveldb.num-files-at-level" + NumberToString(level),
                         &property));
    return atoi(property.c_str());
  }

private:
  std::string dbname_;
  DB* db_;
};

Status ReduceLevelTest::OpenDB(bool create_if_missing, int num_levels,
    int mem_table_compact_level) {
  leveldb::Options opt;
  opt.num_levels = num_levels;
  opt.create_if_missing = create_if_missing;
  opt.max_mem_compaction_level = mem_table_compact_level;
  leveldb::Status st = leveldb::DB::Open(opt, dbname_, &db_);
  if (!st.ok()) {
    fprintf(stderr, "Can't open the db:%s\n", st.ToString().c_str());
  }
  return st;
}

bool ReduceLevelTest::ReduceLevels(int target_level) {
  std::vector<std::string> args = leveldb::ReduceDBLevels::PrepareArgs(
      target_level, false);
  ReduceDBLevels level_reducer(dbname_, args);
  level_reducer.Run();
  return level_reducer.GetExecuteState().IsSucceed();
}

TEST(ReduceLevelTest, Last_Level) {
  // create files on all levels;
  ASSERT_OK(OpenDB(true, 4, 3));
  ASSERT_OK(Put("aaaa", "11111"));
  ASSERT_OK(CompactMemTable());
  ASSERT_EQ(FilesOnLevel(3), 1);
  CloseDB();

  ASSERT_TRUE(ReduceLevels(3));
  ASSERT_OK(OpenDB(true, 3, 1));
  ASSERT_EQ(FilesOnLevel(2), 1);
  CloseDB();

  ASSERT_TRUE(ReduceLevels(2));
  ASSERT_OK(OpenDB(true, 2, 1));
  ASSERT_EQ(FilesOnLevel(1), 1);
  CloseDB();
}

TEST(ReduceLevelTest, Top_Level) {
  // create files on all levels;
  ASSERT_OK(OpenDB(true, 5, 0));
  ASSERT_OK(Put("aaaa", "11111"));
  ASSERT_OK(CompactMemTable());
  ASSERT_EQ(FilesOnLevel(0), 1);
  CloseDB();

  ASSERT_TRUE(ReduceLevels(4));
  ASSERT_OK(OpenDB(true, 4, 0));
  CloseDB();

  ASSERT_TRUE(ReduceLevels(3));
  ASSERT_OK(OpenDB(true, 3, 0));
  CloseDB();

  ASSERT_TRUE(ReduceLevels(2));
  ASSERT_OK(OpenDB(true, 2, 0));
  CloseDB();
}

TEST(ReduceLevelTest, All_Levels) {
  // create files on all levels;
  ASSERT_OK(OpenDB(true, 5, 1));
  ASSERT_OK(Put("a", "a11111"));
  ASSERT_OK(CompactMemTable());
  ASSERT_EQ(FilesOnLevel(1), 1);
  CloseDB();

  ASSERT_OK(OpenDB(true, 5, 2));
  ASSERT_OK(Put("b", "b11111"));
  ASSERT_OK(CompactMemTable());
  ASSERT_EQ(FilesOnLevel(1), 1);
  ASSERT_EQ(FilesOnLevel(2), 1);
  CloseDB();

  ASSERT_OK(OpenDB(true, 5, 3));
  ASSERT_OK(Put("c", "c11111"));
  ASSERT_OK(CompactMemTable());
  ASSERT_EQ(FilesOnLevel(1), 1);
  ASSERT_EQ(FilesOnLevel(2), 1);
  ASSERT_EQ(FilesOnLevel(3), 1);
  CloseDB();

  ASSERT_OK(OpenDB(true, 5, 4));
  ASSERT_OK(Put("d", "d11111"));
  ASSERT_OK(CompactMemTable());
  ASSERT_EQ(FilesOnLevel(1), 1);
  ASSERT_EQ(FilesOnLevel(2), 1);
  ASSERT_EQ(FilesOnLevel(3), 1);
  ASSERT_EQ(FilesOnLevel(4), 1);
  CloseDB();

  ASSERT_TRUE(ReduceLevels(4));
  ASSERT_OK(OpenDB(true, 4, 0));
  ASSERT_EQ("a11111", Get("a"));
  ASSERT_EQ("b11111", Get("b"));
  ASSERT_EQ("c11111", Get("c"));
  ASSERT_EQ("d11111", Get("d"));
  CloseDB();

  ASSERT_TRUE(ReduceLevels(3));
  ASSERT_OK(OpenDB(true, 3, 0));
  ASSERT_EQ("a11111", Get("a"));
  ASSERT_EQ("b11111", Get("b"));
  ASSERT_EQ("c11111", Get("c"));
  ASSERT_EQ("d11111", Get("d"));
  CloseDB();

  ASSERT_TRUE(ReduceLevels(2));
  ASSERT_OK(OpenDB(true, 2, 0));
  ASSERT_EQ("a11111", Get("a"));
  ASSERT_EQ("b11111", Get("b"));
  ASSERT_EQ("c11111", Get("c"));
  ASSERT_EQ("d11111", Get("d"));
  CloseDB();
}

}

int main(int argc, char** argv) {
  return leveldb::test::RunAllTests();
}
