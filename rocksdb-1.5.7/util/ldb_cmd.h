// Copyright (c) 2012 Facebook. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LEVELDB_UTIL_LDB_H_
#define LEVELDB_UTIL_LDB_H_

#include <string>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <algorithm>
#include <stdio.h>

#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/options.h"
#include "leveldb/iterator.h"
#include "leveldb/slice.h"
#include "db/version_set.h"
#include "util/logging.h"

namespace leveldb {

class LDBCommandExecuteResult {
public:
  enum State {
    EXEC_NOT_STARTED = 0, EXEC_SUCCEED = 1, EXEC_FAILED = 2,
  };

  LDBCommandExecuteResult() {
    state_ = EXEC_NOT_STARTED;
    message_ = "";
  }

  LDBCommandExecuteResult(State state, std::string& msg) {
    state_ = state;
    message_ = msg;
  }

  std::string ToString() {
    std::string ret;
    switch (state_) {
    case EXEC_SUCCEED:
      break;
    case EXEC_FAILED:
      ret.append("Failed: ");
      break;
    case EXEC_NOT_STARTED:
      ret.append("Not started: ");
    }
    if (!message_.empty()) {
      ret.append(message_);
    }
    return ret;
  }

  void Reset() {
    state_ = EXEC_NOT_STARTED;
    message_ = "";
  }

  bool IsSucceed() {
    return state_ == EXEC_SUCCEED;
  }

  bool IsNotStarted() {
    return state_ == EXEC_NOT_STARTED;
  }

  bool IsFailed() {
    return state_ == EXEC_FAILED;
  }

  static LDBCommandExecuteResult SUCCEED(std::string msg) {
    return LDBCommandExecuteResult(EXEC_SUCCEED, msg);
  }

  static LDBCommandExecuteResult FAILED(std::string msg) {
    return LDBCommandExecuteResult(EXEC_FAILED, msg);
  }

private:
  State state_;
  std::string message_;

  bool operator==(const LDBCommandExecuteResult&);
  bool operator!=(const LDBCommandExecuteResult&);
};

class LDBCommand {
public:

  /* Constructor */
  LDBCommand(std::string& db_name, std::vector<std::string>& args) :
    db_path_(db_name),
    db_(NULL) {
    parse_open_args(args);
  }

  LDBCommand(std::vector<std::string>& args) :
    db_path_(""),
    db_(NULL) {
    parse_open_args(args);
  }

  virtual leveldb::Options PrepareOptionsForOpenDB();

  virtual bool NoDBOpen() {
    return false;
  }

  virtual ~LDBCommand() {
    if (db_ != NULL) {
      delete db_;
      db_ = NULL;
    }
  }

  /* Print the help message */
  static void Help(std::string& ret) {
    ret.append("--db=DB_PATH [");
    ret.append(LDBCommand::BLOOM_ARG);
    ret.append("<int,e.g.:14>] [");
    ret.append(LDBCommand::COMPRESSION_TYPE_ARG);
    ret.append("<no|snappy|zlib|bzip2>] [");
    ret.append(LDBCommand::BLOCK_SIZE);
    ret.append("<block_size_in_bytes>] [");
    ret.append(LDBCommand::AUTO_COMPACTION);
    ret.append("<true|false>] [");
    ret.append(LDBCommand::WRITE_BUFFER_SIZE_ARG);
    ret.append("<int,e.g.:4194304>] [");
    ret.append(LDBCommand::FILE_SIZE_ARG);
    ret.append("<int,e.g.:2097152>] ");
  }

  /* Run the command, and return the execute result. */
  void Run() {
    if (!exec_state_.IsNotStarted()) {
      return;
    }

    if (db_ == NULL && !NoDBOpen()) {
      OpenDB();
      if (!exec_state_.IsNotStarted()) {
        return;
      }
    }
    
    DoCommand();
    if (exec_state_.IsNotStarted()) {
      exec_state_ = LDBCommandExecuteResult::SUCCEED("");
    }

    if (db_ != NULL) {
      CloseDB ();
    }
  }

  virtual void DoCommand() = 0;

  LDBCommandExecuteResult GetExecuteState() {
    return exec_state_;
  }

  void ClearPreviousRunState() {
    exec_state_.Reset();
  }

  static std::string HexToString(const std::string& str) {
    std::string parsed;
    for (unsigned int i = 0; i < str.length();) {
      int c;
      sscanf(str.c_str() + i, "%2X", &c);
      parsed.push_back(c);
      i += 2;
    }
    return parsed;
  }

  static std::string StringToHex(const std::string& str) {
    std::string result;
    char buf[10];
    for (size_t i = 0; i < str.length(); i++) {
      snprintf(buf, 10, "%02X", (unsigned char)str[i]);
      result += buf;
    }
    return result;
  }

  static const char* DELIM;
  static bool ParseKeyValue(const std::string& line,
                              std::string* key,
                              std::string* value,
                              bool hex) {
    size_t pos = line.find(DELIM);
    if (pos != std::string::npos) {
      (*key) = line.substr(0, pos);
      (*value) = line.substr(pos + strlen(DELIM));
      if (hex) {
        (*key) = HexToString(*key);
        (*value) = HexToString(*value);
      }
      return true;
    } else {
      return false;
    }
  }

  static std::string PrintKeyValue(const std::string& key,
                                   const std::string& value,
                                   bool hex) {
    std::string result;
    result.append(hex ? StringToHex(key) : key);
    result.append(DELIM);
    result.append(hex ? StringToHex(value) : value);
    return result;
  }

protected:

  void OpenDB() {
    leveldb::Options opt = PrepareOptionsForOpenDB();
    if (!exec_state_.IsNotStarted()) {
      return;
    }
    // Open the DB.
    leveldb::Status st = leveldb::DB::Open(opt, db_path_, &db_);
    if (!st.ok()) {
      std::string msg = st.ToString();
      exec_state_ = LDBCommandExecuteResult::FAILED(msg);
    }
  }

  void CloseDB () {
    if (db_ != NULL) {
      delete db_;
      db_ = NULL;
    }
  }

  static const char* FROM_ARG;
  static const char* END_ARG;
  static const char* HEX_ARG;
  LDBCommandExecuteResult exec_state_;
  std::string db_path_;
  leveldb::DB* db_;

private:

  static const char* BLOOM_ARG;
  static const char* COMPRESSION_TYPE_ARG;
  static const char* BLOCK_SIZE;
  static const char* AUTO_COMPACTION;
  static const char* WRITE_BUFFER_SIZE_ARG;
  static const char* FILE_SIZE_ARG;
  std::vector<std::string> open_args_;
  void parse_open_args(std::vector<std::string>& args);
};

class Compactor: public LDBCommand {
public:
  Compactor(std::string& db_name, std::vector<std::string>& args);

  virtual ~Compactor() {}

  static void Help(std::string& ret);

  virtual void DoCommand();

private:
  bool null_from_;
  std::string from_;
  bool null_to_;
  std::string to_;
  bool hex_;
};

class DBDumper: public LDBCommand {
public:
  DBDumper(std::string& db_name, std::vector<std::string>& args);
  virtual ~DBDumper() {}
  static void Help(std::string& ret);
  virtual void DoCommand();
private:
  bool null_from_;
  std::string from_;
  bool null_to_;
  std::string to_;
  int max_keys_;
  bool count_only_;
  bool print_stats_;
  bool hex_;
  bool hex_output_;

  static const char* MAX_KEYS_ARG;
  static const char* COUNT_ONLY_ARG;
  static const char* STATS_ARG;
  static const char* HEX_OUTPUT_ARG;
};

class DBLoader: public LDBCommand {
public:
  DBLoader(std::string& db_name, std::vector<std::string>& args);
  virtual ~DBLoader() {}
  static void Help(std::string& ret);
  virtual void DoCommand();

  virtual leveldb::Options PrepareOptionsForOpenDB();

private:
  bool hex_input_;
  bool create_if_missing_;
  bool disable_wal_;

  static const char* HEX_INPUT_ARG;
  static const char* CREATE_IF_MISSING_ARG;
  static const char* DISABLE_WAL_ARG;
};

class DBQuerier: public LDBCommand {
public:
  DBQuerier(std::string& db_name, std::vector<std::string>& args);
  virtual ~DBQuerier() {}
  static void Help(std::string& ret);
  virtual void DoCommand();

private:
  bool hex_;

  static const char* HEX_ARG;

  static const char* HELP_CMD;
  static const char* GET_CMD;
  static const char* PUT_CMD;
  static const char* DELETE_CMD;
};

class ReduceDBLevels : public LDBCommand {
public:

  ReduceDBLevels (std::string& db_name, std::vector<std::string>& args);

  ~ReduceDBLevels() {}

  virtual leveldb::Options PrepareOptionsForOpenDB();

  virtual void DoCommand();

  virtual bool NoDBOpen() {
    return true;
  }

  static void Help(std::string& msg);
  static std::vector<std::string> PrepareArgs(int new_levels,
      bool print_old_level = false);

private:
  int old_levels_;
  int new_levels_;
  bool print_old_levels_;

  static const char* NEW_LEVLES_ARG;
  static const char* PRINT_OLD_LEVELS_ARG;

  Status GetOldNumOfLevels(leveldb::Options& opt, int* levels);
};

class WALDumper : public LDBCommand {
public:

  WALDumper (std::vector<std::string>& args);

  ~WALDumper() {}

  virtual bool  NoDBOpen() {
    return true;
  }

  static void Help(std::string& ret);
  virtual void DoCommand();
private:
  bool print_header_;
  std::string wal_file_;

  static const char* WAL_FILE_ARG;
};
}
#endif
