// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include "db/db_impl.h"
#include "db/version_set.h"
#include "db/db_statistics.h"
#include "leveldb/cache.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/write_batch.h"
#include "leveldb/statistics.h"
#include "port/port.h"
#include "util/crc32c.h"
#include "util/histogram.h"
#include "util/mutexlock.h"
#include "util/random.h"
#include "util/testutil.h"
#include "hdfs/env_hdfs.h"

// Comma-separated list of operations to run in the specified order
//   Actual benchmarks:
//      fillseq       -- write N values in sequential key order in async mode
//      fillrandom    -- write N values in random key order in async mode
//      overwrite     -- overwrite N values in random key order in async mode
//      fillsync      -- write N/100 values in random key order in sync mode
//      fill100K      -- write N/1000 100K values in random order in async mode
//      deleteseq     -- delete N keys in sequential order
//      deleterandom  -- delete N keys in random order
//      readseq       -- read N times sequentially
//      readreverse   -- read N times in reverse order
//      readrandom    -- read N times in random order
//      readmissing   -- read N missing keys in random order
//      readhot       -- read N times in random order from 1% section of DB
//      seekrandom    -- N random seeks
//      crc32c        -- repeated crc32c of 4K of data
//      acquireload   -- load N*1000 times
//   Meta operations:
//      compact     -- Compact the entire DB
//      stats       -- Print DB stats
//      sstables    -- Print sstable info
//      heapprofile -- Dump a heap profile (if supported by this port)
static const char* FLAGS_benchmarks =
    "fillseq,"
    "fillsync,"
    "fillrandom,"
    "overwrite,"
    "readrandom,"
    "readrandom,"  // Extra run to allow previous compactions to quiesce
    "readseq,"
    "readreverse,"
    "compact,"
    "readrandom,"
    "readseq,"
    "readreverse,"
    "readrandomwriterandom," // mix reads and writes based on FLAGS_readwritepercent
    "fill100K,"
    "crc32c,"
    "snappycomp,"
    "snappyuncomp,"
    "acquireload,"
    ;

// Number of key/values to place in database
static long FLAGS_num = 1000000;

// Number of read operations to do.  If negative, do FLAGS_num reads.
static long FLAGS_reads = -1;

// When ==1 reads use ::Get, when >1 reads use an iterator
static long FLAGS_read_range = 1;

// Seed base for random number generators. When 0 it is deterministic.
static long FLAGS_seed = 0;

// Number of concurrent threads to run.
static int FLAGS_threads = 1;

// Size of each value
static int FLAGS_value_size = 100;

// Arrange to generate values that shrink to this fraction of
// their original size after compression
static double FLAGS_compression_ratio = 0.5;

// Print histogram of operation timings
static bool FLAGS_histogram = false;

// Number of bytes to buffer in memtable before compacting
// (initialized to default value by "main")
static int FLAGS_write_buffer_size = 0;

// The number of in-memory memtables.
// Each memtable is of size FLAGS_write_buffer_size.
// This is initialized to default value of 2 in "main" function.
static int FLAGS_max_write_buffer_number = 0;

// The maximum number of concurrent background compactions
// that can occur in parallel.
// This is initialized to default value of 1 in "main" function.
static int FLAGS_max_background_compactions = 0;

// Number of bytes to use as a cache of uncompressed data.
// Negative means use default settings.
static long FLAGS_cache_size = -1;

// Number of bytes in a block.
static int FLAGS_block_size = 0;

// Maximum number of files to keep open at the same time (use default if == 0)
static int FLAGS_open_files = 0;

// Bloom filter bits per key.
// Negative means use default settings.
static int FLAGS_bloom_bits = -1;

// If true, do not destroy the existing database.  If you set this
// flag and also specify a benchmark that wants a fresh database, that
// benchmark will fail.
static bool FLAGS_use_existing_db = false;

// Use the db with the following name.
static const char* FLAGS_db = NULL;

// Number of shards for the block cache is 2 ** FLAGS_cache_numshardbits.
// Negative means use default settings. This is applied only
// if FLAGS_cache_size is non-negative.
static int FLAGS_cache_numshardbits = -1;

// Verify checksum for every block read from storage
static bool FLAGS_verify_checksum = false;

// Database statistics
static bool FLAGS_statistics = false;
static class leveldb::DBStatistics* dbstats = NULL;

// Number of write operations to do.  If negative, do FLAGS_num reads.
static long FLAGS_writes = -1;

// These default values might change if the hardcoded

// Sync all writes to disk
static bool FLAGS_sync = false;

// If true, do not wait until data is synced to disk.
static bool FLAGS_disable_data_sync = false;

// If true, issue fsync instead of fdatasync
static bool FLAGS_use_fsync = false;

// If true, do not write WAL for write.
static bool FLAGS_disable_wal = false;

// The total number of levels
static unsigned int FLAGS_num_levels = 7;

// Target level-0 file size for compaction
static int FLAGS_target_file_size_base = 2 * 1048576;

// A multiplier to compute targe level-N file size
static int FLAGS_target_file_size_multiplier = 1;

// Max bytes for level-1
static uint64_t FLAGS_max_bytes_for_level_base = 10 * 1048576;

// A multiplier to compute max bytes for level-N
static int FLAGS_max_bytes_for_level_multiplier = 10;

// Number of files in level-0 that will trigger put stop.
static int FLAGS_level0_stop_writes_trigger = 12;

// Number of files in level-0 that will slow down writes.
static int FLAGS_level0_slowdown_writes_trigger = 8;

// Number of files in level-0 when compactions start
static int FLAGS_level0_file_num_compaction_trigger = 4;

// Ratio of reads to writes (expressed as a percentage)
// for the ReadRandomWriteRandom workload. The default
// setting is 9 gets for every 1 put.
static int FLAGS_readwritepercent = 90;

// Option to disable compation triggered by read.
static int FLAGS_disable_seek_compaction = false;

// Option to delete obsolete files periodically
// Default: 0 which means that obsolete files are
// deleted after every compaction run.
static uint64_t FLAGS_delete_obsolete_files_period_micros = 0;

// Algorithm to use to compress the database
static enum leveldb::CompressionType FLAGS_compression_type =
    leveldb::kSnappyCompression;

// Allows compression for levels 0 and 1 to be disabled when
// other levels are compressed
static int FLAGS_min_level_to_compress = -1;

static int FLAGS_table_cache_numshardbits = 4;

// posix or hdfs environment
static leveldb::Env* FLAGS_env = leveldb::Env::Default();

// Stats are reported every N operations when this is greater
// than zero. When 0 the interval grows over time.
static int FLAGS_stats_interval = 0;

// Reports additional stats per interval when this is greater
// than 0.
static int FLAGS_stats_per_interval = 0;

// When not equal to 0 this make threads sleep at each stats
// reporting interval until the compaction score for all levels is
// less than or equal to this value.
static double FLAGS_rate_limit = 0;

// Control maximum bytes of overlaps in grandparent (i.e., level+2) before we
// stop building a single file in a level->level+1 compaction.
static int FLAGS_max_grandparent_overlap_factor = 10;

// Run read only benchmarks.
static bool FLAGS_read_only = false;

// Do not auto trigger compactions
static bool FLAGS_disable_auto_compactions = false;

// Cap the size of data in levelK for a compaction run
// that compacts Levelk with LevelK+1
static int FLAGS_source_compaction_factor = 1;

// Set the TTL for the WAL Files.
static uint64_t FLAGS_WAL_ttl_seconds = 0;

extern bool useOsBuffer;
extern bool useFsReadAhead;
extern bool useMmapRead;
extern bool useMmapWrite;

namespace leveldb {

// Helper for quickly generating random data.
class RandomGenerator {
 private:
  std::string data_;
  unsigned int pos_;

 public:
  RandomGenerator() {
    // We use a limited amount of data over and over again and ensure
    // that it is larger than the compression window (32KB), and also
    // large enough to serve all typical value sizes we want to write.
    Random rnd(301);
    std::string piece;
    while (data_.size() < 1048576) {
      // Add a short fragment that is as compressible as specified
      // by FLAGS_compression_ratio.
      test::CompressibleString(&rnd, FLAGS_compression_ratio, 100, &piece);
      data_.append(piece);
    }
    pos_ = 0;
  }

  Slice Generate(int len) {
    if (pos_ + len > data_.size()) {
      pos_ = 0;
      assert(len < data_.size());
    }
    pos_ += len;
    return Slice(data_.data() + pos_ - len, len);
  }
};
static Slice TrimSpace(Slice s) {
  unsigned int start = 0;
  while (start < s.size() && isspace(s[start])) {
    start++;
  }
  unsigned int limit = s.size();
  while (limit > start && isspace(s[limit-1])) {
    limit--;
  }
  return Slice(s.data() + start, limit - start);
}

static void AppendWithSpace(std::string* str, Slice msg) {
  if (msg.empty()) return;
  if (!str->empty()) {
    str->push_back(' ');
  }
  str->append(msg.data(), msg.size());
}

class Stats {
 private:
  int id_;
  double start_;
  double finish_;
  double seconds_;
  long done_;
  long last_report_done_;
  int next_report_;
  int64_t bytes_;
  double last_op_finish_;
  double last_report_finish_;
  Histogram hist_;
  std::string message_;

 public:
  Stats() { Start(-1); }

  void Start(int id) {
    id_ = id;
    next_report_ = FLAGS_stats_interval ? FLAGS_stats_interval : 100;
    last_op_finish_ = start_;
    hist_.Clear();
    done_ = 0;
    last_report_done_ = 0;
    bytes_ = 0;
    seconds_ = 0;
    start_ = FLAGS_env->NowMicros();
    finish_ = start_;
    last_report_finish_ = start_;
    message_.clear();
  }

  void Merge(const Stats& other) {
    hist_.Merge(other.hist_);
    done_ += other.done_;
    bytes_ += other.bytes_;
    seconds_ += other.seconds_;
    if (other.start_ < start_) start_ = other.start_;
    if (other.finish_ > finish_) finish_ = other.finish_;

    // Just keep the messages from one thread
    if (message_.empty()) message_ = other.message_;
  }

  void Stop() {
    finish_ = FLAGS_env->NowMicros();
    seconds_ = (finish_ - start_) * 1e-6;
  }

  void AddMessage(Slice msg) {
    AppendWithSpace(&message_, msg);
  }

  void SetId(int id) { id_ = id; }

  void FinishedSingleOp(DB* db) {
    if (FLAGS_histogram) {
      double now = FLAGS_env->NowMicros();
      double micros = now - last_op_finish_;
      hist_.Add(micros);
      if (micros > 20000 && !FLAGS_stats_interval) {
        fprintf(stderr, "long op: %.1f micros%30s\r", micros, "");
        fflush(stderr);
      }
      last_op_finish_ = now;
    }

    done_++;
    if (done_ >= next_report_) {
      if (!FLAGS_stats_interval) {
        if      (next_report_ < 1000)   next_report_ += 100;
        else if (next_report_ < 5000)   next_report_ += 500;
        else if (next_report_ < 10000)  next_report_ += 1000;
        else if (next_report_ < 50000)  next_report_ += 5000;
        else if (next_report_ < 100000) next_report_ += 10000;
        else if (next_report_ < 500000) next_report_ += 50000;
        else                            next_report_ += 100000;
        fprintf(stderr, "... finished %ld ops%30s\r", done_, "");
        fflush(stderr);
      } else {
        double now = FLAGS_env->NowMicros();
        fprintf(stderr,
                "%s ... thread %d: (%ld,%ld) ops and (%.1f,%.1f) ops/second in (%.6f,%.6f) seconds\n",
                FLAGS_env->TimeToString((uint64_t) now/1000000).c_str(),
                id_,
                done_ - last_report_done_, done_,
                (done_ - last_report_done_) /
                ((now - last_report_finish_) / 1000000.0),
                done_ / ((now - start_) / 1000000.0),
                (now - last_report_finish_) / 1000000.0,
                (now - start_) / 1000000.0);

        if (FLAGS_stats_per_interval) {
          std::string stats;
          if (db && db->GetProperty("leveldb.stats", &stats))
            fprintf(stderr, "%s\n", stats.c_str());
        }

        fflush(stderr);
        next_report_ += FLAGS_stats_interval;
        last_report_finish_ = now;
        last_report_done_ = done_;
      }
    }
  }

  void AddBytes(int64_t n) {
    bytes_ += n;
  }

  void Report(const Slice& name) {
    // Pretend at least one op was done in case we are running a benchmark
    // that does not call FinishedSingleOp().
    if (done_ < 1) done_ = 1;

    std::string extra;
    if (bytes_ > 0) {
      // Rate is computed on actual elapsed time, not the sum of per-thread
      // elapsed times.
      double elapsed = (finish_ - start_) * 1e-6;
      char rate[100];
      snprintf(rate, sizeof(rate), "%6.1f MB/s",
               (bytes_ / 1048576.0) / elapsed);
      extra = rate;
    }
    AppendWithSpace(&extra, message_);
    double elapsed = (finish_ - start_) * 1e-6;
    double throughput = (double)done_/elapsed;

    fprintf(stdout, "%-12s : %11.3f micros/op %ld ops/sec;%s%s\n",
            name.ToString().c_str(),
            seconds_ * 1e6 / done_,
            (long)throughput,
            (extra.empty() ? "" : " "),
            extra.c_str());
    if (FLAGS_histogram) {
      fprintf(stdout, "Microseconds per op:\n%s\n", hist_.ToString().c_str());
    }
    fflush(stdout);
  }
};

// State shared by all concurrent executions of the same benchmark.
struct SharedState {
  port::Mutex mu;
  port::CondVar cv;
  int total;

  // Each thread goes through the following states:
  //    (1) initializing
  //    (2) waiting for others to be initialized
  //    (3) running
  //    (4) done

  long num_initialized;
  long num_done;
  bool start;

  SharedState() : cv(&mu) { }
};

// Per-thread state for concurrent executions of the same benchmark.
struct ThreadState {
  int tid;             // 0..n-1 when running in n threads
  Random rand;         // Has different seeds for different threads
  Stats stats;
  SharedState* shared;

  /* implicit */ ThreadState(int index)
      : tid(index),
        rand((FLAGS_seed ? FLAGS_seed : 1000) + index) {
  }
};

class Benchmark {
 private:
  Cache* cache_;
  const FilterPolicy* filter_policy_;
  DB* db_;
  long num_;
  int value_size_;
  int entries_per_batch_;
  WriteOptions write_options_;
  long reads_;
  long writes_;
  long readwrites_;
  int heap_counter_;

  void PrintHeader() {
    const int kKeySize = 16;
    PrintEnvironment();
    fprintf(stdout, "Keys:       %d bytes each\n", kKeySize);
    fprintf(stdout, "Values:     %d bytes each (%d bytes after compression)\n",
            FLAGS_value_size,
            static_cast<int>(FLAGS_value_size * FLAGS_compression_ratio + 0.5));
    fprintf(stdout, "Entries:    %ld\n", num_);
    fprintf(stdout, "RawSize:    %.1f MB (estimated)\n",
            ((static_cast<int64_t>(kKeySize + FLAGS_value_size) * num_)
             / 1048576.0));
    fprintf(stdout, "FileSize:   %.1f MB (estimated)\n",
            (((kKeySize + FLAGS_value_size * FLAGS_compression_ratio) * num_)
             / 1048576.0));

    switch (FLAGS_compression_type) {
      case leveldb::kNoCompression:
        fprintf(stdout, "Compression: none\n");
        break;
      case leveldb::kSnappyCompression:
        fprintf(stdout, "Compression: snappy\n");
        break;
      case leveldb::kZlibCompression:
        fprintf(stdout, "Compression: zlib\n");
        break;
      case leveldb::kBZip2Compression:
        fprintf(stdout, "Compression: bzip2\n");
        break;
    }

    PrintWarnings();
    fprintf(stdout, "------------------------------------------------\n");
  }

  void PrintWarnings() {
#if defined(__GNUC__) && !defined(__OPTIMIZE__)
    fprintf(stdout,
            "WARNING: Optimization is disabled: benchmarks unnecessarily slow\n"
            );
#endif
#ifndef NDEBUG
    fprintf(stdout,
            "WARNING: Assertions are enabled; benchmarks unnecessarily slow\n");
#endif

    if (FLAGS_compression_type != leveldb::kNoCompression) {
      // The test string should not be too small.
      const int len = FLAGS_block_size;
      char* text = (char*) malloc(len+1);
      bool result = true;
      const char* name = NULL;
      std::string compressed;

      memset(text, (int) 'y', len);
      text[len] = '\0';

      switch (FLAGS_compression_type) {
        case kSnappyCompression:
          result = port::Snappy_Compress(Options().compression_opts, text,
                                         strlen(text), &compressed);
          name = "Snappy";
          break;
        case kZlibCompression:
          result = port::Zlib_Compress(Options().compression_opts, text,
                                       strlen(text), &compressed);
          name = "Zlib";
          break;
        case kBZip2Compression:
          result = port::BZip2_Compress(Options().compression_opts, text,
                                        strlen(text), &compressed);
          name = "BZip2";
          break;
        case kNoCompression:
          assert(false); // cannot happen
          break;
      }

      if (!result) {
        fprintf(stdout, "WARNING: %s compression is not enabled\n", name);
      } else if (name && compressed.size() >= strlen(text)) {
        fprintf(stdout, "WARNING: %s compression is not effective\n", name);
      }

      free(text);
    }
  }

  void PrintEnvironment() {
    fprintf(stderr, "LevelDB:    version %d.%d\n",
            kMajorVersion, kMinorVersion);

#if defined(__linux)
    time_t now = time(NULL);
    fprintf(stderr, "Date:       %s", ctime(&now));  // ctime() adds newline

    FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo != NULL) {
      char line[1000];
      int num_cpus = 0;
      std::string cpu_type;
      std::string cache_size;
      while (fgets(line, sizeof(line), cpuinfo) != NULL) {
        const char* sep = strchr(line, ':');
        if (sep == NULL) {
          continue;
        }
        Slice key = TrimSpace(Slice(line, sep - 1 - line));
        Slice val = TrimSpace(Slice(sep + 1));
        if (key == "model name") {
          ++num_cpus;
          cpu_type = val.ToString();
        } else if (key == "cache size") {
          cache_size = val.ToString();
        }
      }
      fclose(cpuinfo);
      fprintf(stderr, "CPU:        %d * %s\n", num_cpus, cpu_type.c_str());
      fprintf(stderr, "CPUCache:   %s\n", cache_size.c_str());
    }
#endif
  }

  void PrintStatistics() {
    if (FLAGS_statistics) {
      fprintf(stdout, "File opened:%ld closed:%ld errors:%ld\n"
          "Block Cache Hit Count:%ld Block Cache Miss Count:%ld\n"
          "Bloom Filter Useful: %ld \n"
          "Compaction key_drop_newer_entry: %ld key_drop_obsolete: %ld "
          "Compaction key_drop_user: %ld",
          dbstats->getNumFileOpens(),
          dbstats->getNumFileCloses(),
          dbstats->getNumFileErrors(),
          dbstats->getTickerCount(BLOCK_CACHE_HIT),
          dbstats->getTickerCount(BLOCK_CACHE_MISS),
          dbstats->getTickerCount(BLOOM_FILTER_USEFUL),
          dbstats->getTickerCount(COMPACTION_KEY_DROP_NEWER_ENTRY),
          dbstats->getTickerCount(COMPACTION_KEY_DROP_OBSOLETE),
          dbstats->getTickerCount(COMPACTION_KEY_DROP_USER));
    }
  }

 public:
  Benchmark()
  : cache_(FLAGS_cache_size >= 0 ?
           (FLAGS_cache_numshardbits >= 1 ?
            NewLRUCache(FLAGS_cache_size, FLAGS_cache_numshardbits) :
            NewLRUCache(FLAGS_cache_size)) : NULL),
    filter_policy_(FLAGS_bloom_bits >= 0
                   ? NewBloomFilterPolicy(FLAGS_bloom_bits)
                   : NULL),
    db_(NULL),
    num_(FLAGS_num),
    value_size_(FLAGS_value_size),
    entries_per_batch_(1),
    reads_(FLAGS_reads < 0 ? FLAGS_num : FLAGS_reads),
    writes_(FLAGS_writes < 0 ? FLAGS_num : FLAGS_writes),
    readwrites_((FLAGS_writes < 0  && FLAGS_reads < 0)? FLAGS_num :
                ((FLAGS_writes > FLAGS_reads) ? FLAGS_writes : FLAGS_reads)
               ),
    heap_counter_(0) {
    std::vector<std::string> files;
    FLAGS_env->GetChildren(FLAGS_db, &files);
    for (unsigned int i = 0; i < files.size(); i++) {
      if (Slice(files[i]).starts_with("heap-")) {
        FLAGS_env->DeleteFile(std::string(FLAGS_db) + "/" + files[i]);
      }
    }
    if (!FLAGS_use_existing_db) {
      DestroyDB(FLAGS_db, Options());
    }
  }

  ~Benchmark() {
    delete db_;
    delete cache_;
    delete filter_policy_;
  }

  void Run() {
    PrintHeader();
    Open();

    const char* benchmarks = FLAGS_benchmarks;
    while (benchmarks != NULL) {
      const char* sep = strchr(benchmarks, ',');
      Slice name;
      if (sep == NULL) {
        name = benchmarks;
        benchmarks = NULL;
      } else {
        name = Slice(benchmarks, sep - benchmarks);
        benchmarks = sep + 1;
      }

      // Reset parameters that may be overriddden bwlow
      num_ = FLAGS_num;
      reads_ = (FLAGS_reads < 0 ? FLAGS_num : FLAGS_reads);
      writes_ = (FLAGS_writes < 0 ? FLAGS_num : FLAGS_writes);
      value_size_ = FLAGS_value_size;
      entries_per_batch_ = 1;
      write_options_ = WriteOptions();
      if (FLAGS_sync) {
        write_options_.sync = true;
      }

      write_options_.disableWAL = FLAGS_disable_wal;

      void (Benchmark::*method)(ThreadState*) = NULL;
      bool fresh_db = false;
      int num_threads = FLAGS_threads;

      if (name == Slice("fillseq")) {
        fresh_db = true;
        method = &Benchmark::WriteSeq;
      } else if (name == Slice("fillbatch")) {
        fresh_db = true;
        entries_per_batch_ = 1000;
        method = &Benchmark::WriteSeq;
      } else if (name == Slice("fillrandom")) {
        fresh_db = true;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("overwrite")) {
        fresh_db = false;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("fillsync")) {
        fresh_db = true;
        num_ /= 1000;
        write_options_.sync = true;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("fill100K")) {
        fresh_db = true;
        num_ /= 1000;
        value_size_ = 100 * 1000;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("readseq")) {
        method = &Benchmark::ReadSequential;
      } else if (name == Slice("readreverse")) {
        method = &Benchmark::ReadReverse;
      } else if (name == Slice("readrandom")) {
        method = &Benchmark::ReadRandom;
      } else if (name == Slice("readmissing")) {
        method = &Benchmark::ReadMissing;
      } else if (name == Slice("seekrandom")) {
        method = &Benchmark::SeekRandom;
      } else if (name == Slice("readhot")) {
        method = &Benchmark::ReadHot;
      } else if (name == Slice("readrandomsmall")) {
        reads_ /= 1000;
        method = &Benchmark::ReadRandom;
      } else if (name == Slice("deleteseq")) {
        method = &Benchmark::DeleteSeq;
      } else if (name == Slice("deleterandom")) {
        method = &Benchmark::DeleteRandom;
      } else if (name == Slice("readwhilewriting")) {
        num_threads++;  // Add extra thread for writing
        method = &Benchmark::ReadWhileWriting;
      } else if (name == Slice("readrandomwriterandom")) {
        method = &Benchmark::ReadRandomWriteRandom;
      } else if (name == Slice("compact")) {
        method = &Benchmark::Compact;
      } else if (name == Slice("crc32c")) {
        method = &Benchmark::Crc32c;
      } else if (name == Slice("acquireload")) {
        method = &Benchmark::AcquireLoad;
      } else if (name == Slice("snappycomp")) {
        method = &Benchmark::SnappyCompress;
      } else if (name == Slice("snappyuncomp")) {
        method = &Benchmark::SnappyUncompress;
      } else if (name == Slice("heapprofile")) {
        HeapProfile();
      } else if (name == Slice("stats")) {
        PrintStats("leveldb.stats");
      } else if (name == Slice("sstables")) {
        PrintStats("leveldb.sstables");
      } else {
        if (name != Slice()) {  // No error message for empty name
          fprintf(stderr, "unknown benchmark '%s'\n", name.ToString().c_str());
        }
      }

      if (fresh_db) {
        if (FLAGS_use_existing_db) {
          fprintf(stdout, "%-12s : skipped (--use_existing_db is true)\n",
                  name.ToString().c_str());
          method = NULL;
        } else {
          delete db_;
          db_ = NULL;
          DestroyDB(FLAGS_db, Options());
          Open();
        }
      }

      if (method != NULL) {
        RunBenchmark(num_threads, name, method);
      }
    }
    PrintStatistics();
  }

 private:
  struct ThreadArg {
    Benchmark* bm;
    SharedState* shared;
    ThreadState* thread;
    void (Benchmark::*method)(ThreadState*);
  };

  static void ThreadBody(void* v) {
    ThreadArg* arg = reinterpret_cast<ThreadArg*>(v);
    SharedState* shared = arg->shared;
    ThreadState* thread = arg->thread;
    {
      MutexLock l(&shared->mu);
      shared->num_initialized++;
      if (shared->num_initialized >= shared->total) {
        shared->cv.SignalAll();
      }
      while (!shared->start) {
        shared->cv.Wait();
      }
    }

    thread->stats.Start(thread->tid);
    (arg->bm->*(arg->method))(thread);
    thread->stats.Stop();

    {
      MutexLock l(&shared->mu);
      shared->num_done++;
      if (shared->num_done >= shared->total) {
        shared->cv.SignalAll();
      }
    }
  }

  void RunBenchmark(int n, Slice name,
                    void (Benchmark::*method)(ThreadState*)) {
    SharedState shared;
    shared.total = n;
    shared.num_initialized = 0;
    shared.num_done = 0;
    shared.start = false;

    ThreadArg* arg = new ThreadArg[n];
    for (int i = 0; i < n; i++) {
      arg[i].bm = this;
      arg[i].method = method;
      arg[i].shared = &shared;
      arg[i].thread = new ThreadState(i);
      arg[i].thread->shared = &shared;
      FLAGS_env->StartThread(ThreadBody, &arg[i]);
    }

    shared.mu.Lock();
    while (shared.num_initialized < n) {
      shared.cv.Wait();
    }

    shared.start = true;
    shared.cv.SignalAll();
    while (shared.num_done < n) {
      shared.cv.Wait();
    }
    shared.mu.Unlock();

    for (int i = 1; i < n; i++) {
      arg[0].thread->stats.Merge(arg[i].thread->stats);
    }
    arg[0].thread->stats.Report(name);

    for (int i = 0; i < n; i++) {
      delete arg[i].thread;
    }
    delete[] arg;
  }

  void Crc32c(ThreadState* thread) {
    // Checksum about 500MB of data total
    const int size = 4096;
    const char* label = "(4K per op)";
    std::string data(size, 'x');
    int64_t bytes = 0;
    uint32_t crc = 0;
    while (bytes < 500 * 1048576) {
      crc = crc32c::Value(data.data(), size);
      thread->stats.FinishedSingleOp(NULL);
      bytes += size;
    }
    // Print so result is not dead
    fprintf(stderr, "... crc=0x%x\r", static_cast<unsigned int>(crc));

    thread->stats.AddBytes(bytes);
    thread->stats.AddMessage(label);
  }

  void AcquireLoad(ThreadState* thread) {
    int dummy;
    port::AtomicPointer ap(&dummy);
    int count = 0;
    void *ptr = NULL;
    thread->stats.AddMessage("(each op is 1000 loads)");
    while (count < 100000) {
      for (int i = 0; i < 1000; i++) {
        ptr = ap.Acquire_Load();
      }
      count++;
      thread->stats.FinishedSingleOp(NULL);
    }
    if (ptr == NULL) exit(1); // Disable unused variable warning.
  }

  void SnappyCompress(ThreadState* thread) {
    RandomGenerator gen;
    Slice input = gen.Generate(Options().block_size);
    int64_t bytes = 0;
    int64_t produced = 0;
    bool ok = true;
    std::string compressed;
    while (ok && bytes < 1024 * 1048576) {  // Compress 1G
      ok = port::Snappy_Compress(Options().compression_opts, input.data(),
                                 input.size(), &compressed);
      produced += compressed.size();
      bytes += input.size();
      thread->stats.FinishedSingleOp(NULL);
    }

    if (!ok) {
      thread->stats.AddMessage("(snappy failure)");
    } else {
      char buf[100];
      snprintf(buf, sizeof(buf), "(output: %.1f%%)",
               (produced * 100.0) / bytes);
      thread->stats.AddMessage(buf);
      thread->stats.AddBytes(bytes);
    }
  }

  void SnappyUncompress(ThreadState* thread) {
    RandomGenerator gen;
    Slice input = gen.Generate(Options().block_size);
    std::string compressed;
    bool ok = port::Snappy_Compress(Options().compression_opts, input.data(),
                                    input.size(), &compressed);
    int64_t bytes = 0;
    char* uncompressed = new char[input.size()];
    while (ok && bytes < 1024 * 1048576) {  // Compress 1G
      ok =  port::Snappy_Uncompress(compressed.data(), compressed.size(),
                                    uncompressed);
      bytes += input.size();
      thread->stats.FinishedSingleOp(NULL);
    }
    delete[] uncompressed;

    if (!ok) {
      thread->stats.AddMessage("(snappy failure)");
    } else {
      thread->stats.AddBytes(bytes);
    }
  }

  void Open() {
    assert(db_ == NULL);
    Options options;
    options.create_if_missing = !FLAGS_use_existing_db;
    options.block_cache = cache_;
    if (cache_ == NULL) {
      options.no_block_cache = true;
    }
    options.write_buffer_size = FLAGS_write_buffer_size;
    options.max_write_buffer_number = FLAGS_max_write_buffer_number;
    options.max_background_compactions = FLAGS_max_background_compactions;
    options.block_size = FLAGS_block_size;
    options.filter_policy = filter_policy_;
    options.max_open_files = FLAGS_open_files;
    options.statistics = dbstats;
    options.env = FLAGS_env;
    options.disableDataSync = FLAGS_disable_data_sync;
    options.use_fsync = FLAGS_use_fsync;
    options.num_levels = FLAGS_num_levels;
    options.target_file_size_base = FLAGS_target_file_size_base;
    options.target_file_size_multiplier = FLAGS_target_file_size_multiplier;
    options.max_bytes_for_level_base = FLAGS_max_bytes_for_level_base;
    options.max_bytes_for_level_multiplier =
        FLAGS_max_bytes_for_level_multiplier;
    options.level0_stop_writes_trigger = FLAGS_level0_stop_writes_trigger;
    options.level0_file_num_compaction_trigger =
        FLAGS_level0_file_num_compaction_trigger;
    options.level0_slowdown_writes_trigger =
      FLAGS_level0_slowdown_writes_trigger;
    options.compression = FLAGS_compression_type;
    options.WAL_ttl_seconds = FLAGS_WAL_ttl_seconds;
    if (FLAGS_min_level_to_compress >= 0) {
      assert(FLAGS_min_level_to_compress <= FLAGS_num_levels);
      options.compression_per_level = new CompressionType[FLAGS_num_levels];
      for (int i = 0; i < FLAGS_min_level_to_compress; i++) {
        options.compression_per_level[i] = kNoCompression;
      }
      for (unsigned int i = FLAGS_min_level_to_compress;
           i < FLAGS_num_levels; i++) {
        options.compression_per_level[i] = FLAGS_compression_type;
      }
    }
    options.disable_seek_compaction = FLAGS_disable_seek_compaction;
    options.delete_obsolete_files_period_micros =
      FLAGS_delete_obsolete_files_period_micros;
    options.rate_limit = FLAGS_rate_limit;
    options.table_cache_numshardbits = FLAGS_table_cache_numshardbits;
    options.max_grandparent_overlap_factor =
      FLAGS_max_grandparent_overlap_factor;
    options.disable_auto_compactions = FLAGS_disable_auto_compactions;
    options.source_compaction_factor = FLAGS_source_compaction_factor;
    Status s;
    if(FLAGS_read_only) {
      s = DB::OpenForReadOnly(options, FLAGS_db, &db_);
    } else {
      s = DB::Open(options, FLAGS_db, &db_);
    }
    if (!s.ok()) {
      fprintf(stderr, "open error: %s\n", s.ToString().c_str());
      exit(1);
    }
    if (FLAGS_min_level_to_compress >= 0) {
      delete options.compression_per_level;
    }
  }

  void WriteSeq(ThreadState* thread) {
    DoWrite(thread, true);
  }

  void WriteRandom(ThreadState* thread) {
    DoWrite(thread, false);
  }

  void DoWrite(ThreadState* thread, bool seq) {
    if (num_ != FLAGS_num) {
      char msg[100];
      snprintf(msg, sizeof(msg), "(%ld ops)", num_);
      thread->stats.AddMessage(msg);
    }

    RandomGenerator gen;
    WriteBatch batch;
    Status s;
    int64_t bytes = 0;
    for (int i = 0; i < writes_; i += entries_per_batch_) {
      batch.Clear();
      for (int j = 0; j < entries_per_batch_; j++) {
        const int k = seq ? i+j : (thread->rand.Next() % FLAGS_num);
        char key[100];
        snprintf(key, sizeof(key), "%016d", k);
        batch.Put(key, gen.Generate(value_size_));
        bytes += value_size_ + strlen(key);
        thread->stats.FinishedSingleOp(db_);
      }
      s = db_->Write(write_options_, &batch);
      if (!s.ok()) {
        fprintf(stderr, "put error: %s\n", s.ToString().c_str());
        exit(1);
      }
    }
    thread->stats.AddBytes(bytes);
  }

  void ReadSequential(ThreadState* thread) {
    Iterator* iter = db_->NewIterator(ReadOptions(FLAGS_verify_checksum, true));
    long i = 0;
    int64_t bytes = 0;
    for (iter->SeekToFirst(); i < reads_ && iter->Valid(); iter->Next()) {
      bytes += iter->key().size() + iter->value().size();
      thread->stats.FinishedSingleOp(db_);
      ++i;
    }
    delete iter;
    thread->stats.AddBytes(bytes);
  }

  void ReadReverse(ThreadState* thread) {
    Iterator* iter = db_->NewIterator(ReadOptions(FLAGS_verify_checksum, true));
    long i = 0;
    int64_t bytes = 0;
    for (iter->SeekToLast(); i < reads_ && iter->Valid(); iter->Prev()) {
      bytes += iter->key().size() + iter->value().size();
      thread->stats.FinishedSingleOp(db_);
      ++i;
    }
    delete iter;
    thread->stats.AddBytes(bytes);
  }

  void ReadRandom(ThreadState* thread) {
    ReadOptions options(FLAGS_verify_checksum, true);
    Iterator* iter = db_->NewIterator(options);

    std::string value;
    long found = 0;
    for (long i = 0; i < reads_; i++) {
      char key[100];
      const int k = thread->rand.Next() % FLAGS_num;
      snprintf(key, sizeof(key), "%016d", k);

      if (FLAGS_read_range < 2) {
        if (db_->Get(options, key, &value).ok()) {
          found++;
        }
      } else {
        Slice skey(key);
        int count = 1;
        for (iter->Seek(skey);
             iter->Valid() && count <= FLAGS_read_range;
             ++count, iter->Next()) {
          found++;
        }
      }

      thread->stats.FinishedSingleOp(db_);
    }
    delete iter;
    char msg[100];
    snprintf(msg, sizeof(msg), "(%ld of %ld found)", found, num_);
    thread->stats.AddMessage(msg);
  }

  void ReadMissing(ThreadState* thread) {
    ReadOptions options(FLAGS_verify_checksum, true);
    std::string value;
    for (long i = 0; i < reads_; i++) {
      char key[100];
      const int k = thread->rand.Next() % FLAGS_num;
      snprintf(key, sizeof(key), "%016d.", k);
      db_->Get(options, key, &value);
      thread->stats.FinishedSingleOp(db_);
    }
  }

  void ReadHot(ThreadState* thread) {
    ReadOptions options(FLAGS_verify_checksum, true);
    std::string value;
    const long range = (FLAGS_num + 99) / 100;
    for (long i = 0; i < reads_; i++) {
      char key[100];
      const int k = thread->rand.Next() % range;
      snprintf(key, sizeof(key), "%016d", k);
      db_->Get(options, key, &value);
      thread->stats.FinishedSingleOp(db_);
    }
  }

  void SeekRandom(ThreadState* thread) {
    ReadOptions options(FLAGS_verify_checksum, true);
    std::string value;
    long found = 0;
    for (long i = 0; i < reads_; i++) {
      Iterator* iter = db_->NewIterator(options);
      char key[100];
      const int k = thread->rand.Next() % FLAGS_num;
      snprintf(key, sizeof(key), "%016d", k);
      iter->Seek(key);
      if (iter->Valid() && iter->key() == key) found++;
      delete iter;
      thread->stats.FinishedSingleOp(db_);
    }
    char msg[100];
    snprintf(msg, sizeof(msg), "(%ld of %ld found)", found, num_);
    thread->stats.AddMessage(msg);
  }

  void DoDelete(ThreadState* thread, bool seq) {
    RandomGenerator gen;
    WriteBatch batch;
    Status s;
    for (int i = 0; i < num_; i += entries_per_batch_) {
      batch.Clear();
      for (int j = 0; j < entries_per_batch_; j++) {
        const int k = seq ? i+j : (thread->rand.Next() % FLAGS_num);
        char key[100];
        snprintf(key, sizeof(key), "%016d", k);
        batch.Delete(key);
        thread->stats.FinishedSingleOp(db_);
      }
      s = db_->Write(write_options_, &batch);
      if (!s.ok()) {
        fprintf(stderr, "del error: %s\n", s.ToString().c_str());
        exit(1);
      }
    }
  }

  void DeleteSeq(ThreadState* thread) {
    DoDelete(thread, true);
  }

  void DeleteRandom(ThreadState* thread) {
    DoDelete(thread, false);
  }

  void ReadWhileWriting(ThreadState* thread) {
    if (thread->tid > 0) {
      ReadRandom(thread);
    } else {
      // Special thread that keeps writing until other threads are done.
      RandomGenerator gen;
      while (true) {
        {
          MutexLock l(&thread->shared->mu);
          if (thread->shared->num_done + 1 >= thread->shared->num_initialized) {
            // Other threads have finished
            break;
          }
        }

        const int k = thread->rand.Next() % FLAGS_num;
        char key[100];
        snprintf(key, sizeof(key), "%016d", k);
        Status s = db_->Put(write_options_, key, gen.Generate(value_size_));
        if (!s.ok()) {
          fprintf(stderr, "put error: %s\n", s.ToString().c_str());
          exit(1);
        }
      }

      // Do not count any of the preceding work/delay in stats.
      thread->stats.Start(thread->tid);
    }
  }

  //
  // This is diffferent from ReadWhileWriting because it does not use
  // an extra thread.
  //
  void ReadRandomWriteRandom(ThreadState* thread) {
    ReadOptions options(FLAGS_verify_checksum, true);
    RandomGenerator gen;
    std::string value;
    long found = 0;
    int get_weight = 0;
    int put_weight = 0;
    long reads_done = 0;
    long writes_done = 0;
    // the number of iterations is the larger of read_ or write_
    for (long i = 0; i < readwrites_; i++) {
      char key[100];
      const int k = thread->rand.Next() % FLAGS_num;
      snprintf(key, sizeof(key), "%016d", k);
      if (get_weight == 0 && put_weight == 0) {
        // one batch complated, reinitialize for next batch
        get_weight = FLAGS_readwritepercent;
        put_weight = 100 - get_weight;
      }
      if (get_weight > 0) {
        // do all the gets first
        if (db_->Get(options, key, &value).ok()) {
          found++;
        }
        get_weight--;
        reads_done++;
      } else  if (put_weight > 0) {
        // then do all the corresponding number of puts
        // for all the gets we have done earlier
        Status s = db_->Put(write_options_, key, gen.Generate(value_size_));
        if (!s.ok()) {
          fprintf(stderr, "put error: %s\n", s.ToString().c_str());
          exit(1);
        }
        put_weight--;
        writes_done++;
      }
      thread->stats.FinishedSingleOp(db_);
    }
    char msg[100];
    snprintf(msg, sizeof(msg), "( reads:%ld writes:%ld total:%ld )",
             reads_done, writes_done, readwrites_);
    thread->stats.AddMessage(msg);
  }

  void Compact(ThreadState* thread) {
    db_->CompactRange(NULL, NULL);
  }

  void PrintStats(const char* key) {
    std::string stats;
    if (!db_->GetProperty(key, &stats)) {
      stats = "(failed)";
    }
    fprintf(stdout, "\n%s\n", stats.c_str());
  }

  static void WriteToFile(void* arg, const char* buf, int n) {
    reinterpret_cast<WritableFile*>(arg)->Append(Slice(buf, n));
  }

  void HeapProfile() {
    char fname[100];
    snprintf(fname, sizeof(fname), "%s/heap-%04d", FLAGS_db, ++heap_counter_);
    WritableFile* file;
    Status s = FLAGS_env->NewWritableFile(fname, &file);
    if (!s.ok()) {
      fprintf(stderr, "%s\n", s.ToString().c_str());
      return;
    }
    bool ok = port::GetHeapProfile(WriteToFile, file);
    delete file;
    if (!ok) {
      fprintf(stderr, "heap profiling not supported\n");
      FLAGS_env->DeleteFile(fname);
    }
  }
};

}  // namespace leveldb

int main(int argc, char** argv) {
  FLAGS_write_buffer_size = leveldb::Options().write_buffer_size;
  FLAGS_max_write_buffer_number = leveldb::Options().max_write_buffer_number;
  FLAGS_open_files = leveldb::Options().max_open_files;
  FLAGS_max_background_compactions =
    leveldb::Options().max_background_compactions;
  // Compression test code above refers to FLAGS_block_size
  FLAGS_block_size = leveldb::Options().block_size;
  std::string default_db_path;

  for (int i = 1; i < argc; i++) {
    double d;
    int n;
    long l;
    char junk;
    char hdfsname[2048];
    if (leveldb::Slice(argv[i]).starts_with("--benchmarks=")) {
      FLAGS_benchmarks = argv[i] + strlen("--benchmarks=");
    } else if (sscanf(argv[i], "--compression_ratio=%lf%c", &d, &junk) == 1) {
      FLAGS_compression_ratio = d;
    } else if (sscanf(argv[i], "--histogram=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_histogram = n;
    } else if (sscanf(argv[i], "--use_existing_db=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_use_existing_db = n;
    } else if (sscanf(argv[i], "--num=%ld%c", &l, &junk) == 1) {
      FLAGS_num = l;
    } else if (sscanf(argv[i], "--reads=%d%c", &n, &junk) == 1) {
      FLAGS_reads = n;
    } else if (sscanf(argv[i], "--read_range=%d%c", &n, &junk) == 1) {
      FLAGS_read_range = n;
    } else if (sscanf(argv[i], "--seed=%ld%c", &l, &junk) == 1) {
      FLAGS_seed = l;
    } else if (sscanf(argv[i], "--threads=%d%c", &n, &junk) == 1) {
      FLAGS_threads = n;
    } else if (sscanf(argv[i], "--value_size=%d%c", &n, &junk) == 1) {
      FLAGS_value_size = n;
    } else if (sscanf(argv[i], "--write_buffer_size=%d%c", &n, &junk) == 1) {
      FLAGS_write_buffer_size = n;
    } else if (sscanf(argv[i], "--max_write_buffer_number=%d%c", &n, &junk) == 1) {
      FLAGS_max_write_buffer_number = n;
    } else if (sscanf(argv[i], "--max_background_compactions=%d%c", &n, &junk) == 1) {
      FLAGS_max_background_compactions = n;
    } else if (sscanf(argv[i], "--cache_size=%ld%c", &l, &junk) == 1) {
      FLAGS_cache_size = l;
    } else if (sscanf(argv[i], "--block_size=%d%c", &n, &junk) == 1) {
      FLAGS_block_size = n;
    } else if (sscanf(argv[i], "--cache_numshardbits=%d%c", &n, &junk) == 1) {
      if (n < 20) {
        FLAGS_cache_numshardbits = n;
      } else {
        fprintf(stderr, "The cache cannot be sharded into 2**%d pieces\n", n);
        exit(1);
      }
    } else if (sscanf(argv[i], "--table_cache_numshardbits=%d%c",
          &n, &junk) == 1) {
      if (n <= 0 || n > 20) {
        fprintf(stderr, "The cache cannot be sharded into 2**%d pieces\n", n);
        exit(1);
      }
      FLAGS_table_cache_numshardbits = n;
    } else if (sscanf(argv[i], "--bloom_bits=%d%c", &n, &junk) == 1) {
      FLAGS_bloom_bits = n;
    } else if (sscanf(argv[i], "--open_files=%d%c", &n, &junk) == 1) {
      FLAGS_open_files = n;
    } else if (strncmp(argv[i], "--db=", 5) == 0) {
      FLAGS_db = argv[i] + 5;
    } else if (sscanf(argv[i], "--verify_checksum=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_verify_checksum = n;
    } else if (sscanf(argv[i], "--bufferedio=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      useOsBuffer = n;
    } else if (sscanf(argv[i], "--mmap_read=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      useMmapRead = n;
    } else if (sscanf(argv[i], "--mmap_write=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      useMmapWrite = n;
    } else if (sscanf(argv[i], "--readahead=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      useFsReadAhead = n;
    } else if (sscanf(argv[i], "--statistics=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      if (n == 1) {
        dbstats = new leveldb::DBStatistics();
        FLAGS_statistics = true;
      }
    } else if (sscanf(argv[i], "--writes=%d%c", &n, &junk) == 1) {
      FLAGS_writes = n;
    } else if (sscanf(argv[i], "--sync=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_sync = n;
    } else if (sscanf(argv[i], "--readwritepercent=%d%c", &n, &junk) == 1 &&
               n > 0 && n < 100) {
      FLAGS_readwritepercent = n;
    } else if (sscanf(argv[i], "--disable_data_sync=%d%c", &n, &junk) == 1 &&
        (n == 0 || n == 1)) {
      FLAGS_disable_data_sync = n;
    } else if (sscanf(argv[i], "--use_fsync=%d%c", &n, &junk) == 1 &&
        (n == 0 || n == 1)) {
      FLAGS_use_fsync = n;
    } else if (sscanf(argv[i], "--disable_wal=%d%c", &n, &junk) == 1 &&
        (n == 0 || n == 1)) {
      FLAGS_disable_wal = n;
    } else if (sscanf(argv[i], "--hdfs=%s", hdfsname) == 1) {
      FLAGS_env  = new leveldb::HdfsEnv(hdfsname);
    } else if (sscanf(argv[i], "--num_levels=%d%c",
        &n, &junk) == 1) {
      FLAGS_num_levels = n;
    } else if (sscanf(argv[i], "--target_file_size_base=%d%c",
        &n, &junk) == 1) {
      FLAGS_target_file_size_base = n;
    } else if ( sscanf(argv[i], "--target_file_size_multiplier=%d%c",
        &n, &junk) == 1) {
      FLAGS_target_file_size_multiplier = n;
    } else if (
        sscanf(argv[i], "--max_bytes_for_level_base=%ld%c", &l, &junk) == 1) {
      FLAGS_max_bytes_for_level_base = l;
    } else if (sscanf(argv[i], "--max_bytes_for_level_multiplier=%d%c",
        &n, &junk) == 1) {
      FLAGS_max_bytes_for_level_multiplier = n;
    } else if (sscanf(argv[i],"--level0_stop_writes_trigger=%d%c",
        &n, &junk) == 1) {
      FLAGS_level0_stop_writes_trigger = n;
    } else if (sscanf(argv[i],"--level0_slowdown_writes_trigger=%d%c",
        &n, &junk) == 1) {
      FLAGS_level0_slowdown_writes_trigger = n;
    } else if (sscanf(argv[i],"--level0_file_num_compaction_trigger=%d%c",
        &n, &junk) == 1) {
      FLAGS_level0_file_num_compaction_trigger = n;
    } else if (strncmp(argv[i], "--compression_type=", 19) == 0) {
      const char* ctype = argv[i] + 19;
      if (!strcasecmp(ctype, "none"))
        FLAGS_compression_type = leveldb::kNoCompression;
      else if (!strcasecmp(ctype, "snappy"))
        FLAGS_compression_type = leveldb::kSnappyCompression;
      else if (!strcasecmp(ctype, "zlib"))
        FLAGS_compression_type = leveldb::kZlibCompression;
      else if (!strcasecmp(ctype, "bzip2"))
        FLAGS_compression_type = leveldb::kBZip2Compression;
      else {
        fprintf(stdout, "Cannot parse %s\n", argv[i]);
      }
    } else if (sscanf(argv[i], "--min_level_to_compress=%d%c", &n, &junk) == 1
        && n >= 0) {
      FLAGS_min_level_to_compress = n;
    } else if (sscanf(argv[i], "--disable_seek_compaction=%d%c", &n, &junk) == 1
        && (n == 0 || n == 1)) {
      FLAGS_disable_seek_compaction = n;
    } else if (sscanf(argv[i], "--delete_obsolete_files_period_micros=%ld%c",
                      &l, &junk) == 1) {
      FLAGS_delete_obsolete_files_period_micros = l;
    } else if (sscanf(argv[i], "--stats_interval=%d%c", &n, &junk) == 1 &&
               n >= 0 && n < 2000000000) {
      FLAGS_stats_interval = n;
    } else if (sscanf(argv[i], "--stats_per_interval=%d%c", &n, &junk) == 1
        && (n == 0 || n == 1)) {
      FLAGS_stats_per_interval = n;
    } else if (sscanf(argv[i], "--rate_limit=%lf%c", &d, &junk) == 1 &&
               d > 1.0) {
      FLAGS_rate_limit = d;
    } else if (sscanf(argv[i], "--readonly=%d%c", &n, &junk) == 1 &&
        (n == 0 || n ==1 )) {
      FLAGS_read_only = n;
    } else if (sscanf(argv[i], "--max_grandparent_overlap_factor=%d%c",
               &n, &junk) == 1) {
      FLAGS_max_grandparent_overlap_factor = n;
    } else if (sscanf(argv[i], "--disable_auto_compactions=%d%c",
               &n, &junk) == 1 && (n == 0 || n ==1)) {
      FLAGS_disable_auto_compactions = n;
    } else if (sscanf(argv[i], "--source_compaction_factor=%d%c",
               &n, &junk) == 1 && n > 0) {
      FLAGS_source_compaction_factor = n;
    } else if (sscanf(argv[i], "--wal_ttl=%d%c", &n, &junk) == 1) {
      FLAGS_WAL_ttl_seconds = static_cast<uint64_t>(n);
    } else {
      fprintf(stderr, "Invalid flag '%s'\n", argv[i]);
      exit(1);
    }
  }

  // The number of background threads should be at least as much the
  // max number of concurrent compactions.
  FLAGS_env->SetBackgroundThreads(FLAGS_max_background_compactions);

  // Choose a location for the test database if none given with --db=<path>
  if (FLAGS_db == NULL) {
      leveldb::Env::Default()->GetTestDirectory(&default_db_path);
      default_db_path += "/dbbench";
      FLAGS_db = default_db_path.c_str();
  }

  leveldb::Benchmark benchmark;
  benchmark.Run();
  return 0;
}