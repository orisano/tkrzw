/*************************************************************************************************
 * Datatabase manager interface
 *
 * Copyright 2020 Google LLC
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *     https://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software distributed under the
 * License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied.  See the License for the specific language governing permissions
 * and limitations under the License.
 *************************************************************************************************/

#ifndef _TKRZW_DBM_H
#define _TKRZW_DBM_H

#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include <cinttypes>

#include "tkrzw_file.h"
#include "tkrzw_lib_common.h"
#include "tkrzw_str_util.h"

namespace tkrzw {

/**
 * Interface of database manager.
 */
class DBM {
 public:
  /**
   * Interface of processor for a record.
   */
  class RecordProcessor {
   public:
    /**
     * The special string indicating no operation.
     * The uniqueness comes from the address of the data region.  So, checking should be done
     * like your_value.data() == NOOP.data().
     */
    static const std::string_view NOOP;
    /**
     * The special string indicating removing operation.
     * The uniqueness comes from the address of the data region.  So, checking should be done
     * like your_value.data() == REMOVE.data().
     */
    static const std::string_view REMOVE;

    /**
     * Destructor.
     */
    virtual ~RecordProcessor() = default;

    /**
     * Processes an existing record.
     * @param key The key of the existing record.
     * @param value The value of the existing record.
     * @return A string reference to NOOP, REMOVE, or a string of a new value.
     * @details The memory referred to by the return value must be alive until the end of
     * the life-span of this object or until this function is called next time.
     */
    virtual std::string_view ProcessFull(std::string_view key, std::string_view value) {
      return NOOP;
    }

    /**
     * Processes an empty record space.
     * @param key The key specified by the caller.
     * @return A string reference to NOOP, REMOVE, or the new value.
     * @details The memory referred to by the return value must be alive until the end of
     * the life-span of this object or until this function is called next time.
     */
    virtual std::string_view ProcessEmpty(std::string_view key) {
      return NOOP;
    }
  };

  /**
   * Lambda function type to process a record.
   * @details The first parameter is the key of the record.  The second parameter is the value
   * of the existing record, or NOOP if it the record doesn't exist.  The return value is a
   * string reference to NOOP, REMOVE, or the new record value.
   */
  typedef std::function<std::string_view(std::string_view, std::string_view)> RecordLambdaType;

  /**
   * Record processor to implement DBM::Process with a lambda function.
   */
  class RecordProcessorLambda final : public RecordProcessor {
   public:
    /**
     * Constructor.
     * @param proc_lambda A lambda function to process a record.
     */
    explicit RecordProcessorLambda(RecordLambdaType proc_lambda) : proc_lambda_(proc_lambda) {}

    /**
     * Processes an existing record.
     */
    std::string_view ProcessFull(std::string_view key, std::string_view value) override {
      return proc_lambda_(key, value);
    }

    /**
     * Processes an empty record space.
     */
    std::string_view ProcessEmpty(std::string_view key) override {
      return proc_lambda_(key, NOOP);
    }

   private:
    // Lambda function to process a record.
    RecordLambdaType proc_lambda_;
  };

  /**
   * Record processor to implement DBM::Get.
   */
  class RecordProcessorGet final : public RecordProcessor {
   public:
    /**
     * Constructor.
     * @param status The pointer to a status object to contain the result status.
     * @param value The pointer to a string object to contain the result value.
     */
    RecordProcessorGet(Status* status, std::string* value) : status_(status), value_(value) {}

    /**
     * Processes an existing record.
     */
    std::string_view ProcessFull(std::string_view key, std::string_view value) override {
      if (value_ != nullptr) {
        *value_ = value;
      }
      return NOOP;
    }

    /**
     * Processes an empty record space.
     */
    std::string_view ProcessEmpty(std::string_view key) override {
      status_->Set(Status::NOT_FOUND_ERROR);
      return NOOP;
    }

   private:
    /** Status to report. */
    Status* status_;
    /** Value to report. */
    std::string* value_;
  };

  /**
   * Record processor to implement DBM::Set.
   */
  class RecordProcessorSet final : public RecordProcessor {
   public:
    /**
     * Constructor.
     * @param status The pointer to a status object to contain the result status.
     * @param value A string of the value to set.
     * @param overwrite Whether to overwrite the existing value.
     * @param old_value The pointer to a string object to contain the existing value.
     */
    RecordProcessorSet(Status* status, std::string_view value, bool overwrite,
                       std::string* old_value)
        : status_(status), value_(value), overwrite_(overwrite), old_value_(old_value) {}

    /**
     * Processes an existing record.
     */
    std::string_view ProcessFull(std::string_view key, std::string_view value) override {
      if (old_value_ != nullptr) {
        *old_value_ = value;
      }
      if (overwrite_) {
        return value_;
      }
      status_->Set(Status::DUPLICATION_ERROR);
      return NOOP;
    }

    /**
     * Processes an empty record space.
     */
    std::string_view ProcessEmpty(std::string_view key) override {
      return value_;
    }

   private:
    /** Status to report. */
    Status* status_;
    /** Value to store. */
    std::string_view value_;
    /** True to overwrite the existing value. */
    bool overwrite_;
    /** String to store the old value. */
    std::string* old_value_;
  };

  /**
   * Record processor to implement DBM::Remove.
   */
  class RecordProcessorRemove final : public RecordProcessor {
   public:
    /**
     * Constructor.
     * @param status The pointer to a status object to contain the result status.
     * @param old_value The pointer to a string object to contain the existing value.
     */
    explicit RecordProcessorRemove(Status* status, std::string* old_value)
        : status_(status), old_value_(old_value) {}

    /**
     * Processes an existing record.
     */
    std::string_view ProcessFull(std::string_view key, std::string_view value) override {
      if (old_value_ != nullptr) {
        *old_value_ = value;
      }
      return REMOVE;
    }

    /**
     * Processes an empty record space.
     */
    std::string_view ProcessEmpty(std::string_view key) override {
      status_->Set(Status::NOT_FOUND_ERROR);
      return NOOP;
    }

   private:
    /** Status to report. */
    Status* status_;
    /** String to store the old value. */
    std::string* old_value_;
  };

  /**
   * Record processor to implement DBM::Append.
   */
  class RecordProcessorAppend final : public RecordProcessor {
   public:
    /**
     * Constructor.
     * @param value A string of the value to set.
     * @param delim A string of the delimiter.
     */
    RecordProcessorAppend(std::string_view value, std::string_view delim)
        : value_(value), delim_(delim) {}

    /**
     * Processes an existing record.
     */
    std::string_view ProcessFull(std::string_view key, std::string_view value) override {
      if (delim_.empty()) {
        new_value_.reserve(value.size() + value_.size());
        new_value_.append(value);
        new_value_.append(value_);
      } else {
        new_value_.reserve(value.size() + delim_.size() + value_.size());
        new_value_.append(value);
        new_value_.append(delim_);
        new_value_.append(value_);
      }
      return new_value_;
    }

    /**
     * Processes an empty record space.
     */
    std::string_view ProcessEmpty(std::string_view key) override {
      return value_;
    }

   private:
    /** Value to store. */
    std::string_view value_;
    /** Delimiter after the existing value. */
    std::string_view delim_;
    /** The new value. */
    std::string new_value_;
  };

  /**
   * Record processor to implement DBM::CompareExchange.
   */
  class RecordProcessorCompareExchange final : public DBM::RecordProcessor {
   public:
    /**
     * Constructor.
     * @param status The pointer to a status object to contain the result status.
     * @param expected A string of the expected value.
     * @param desired A string of the expected value.
     * @param actual The pointer to a string object to contain the actual value.  If it is
     * nullptr, it is ignored.
     */
    RecordProcessorCompareExchange(Status* status, std::string_view expected,
                                   std::string_view desired, std::string* actual)
        : status_(status), expected_(expected), desired_(desired), actual_(actual) {}

    /**
     * Processes an existing record.
     */
    std::string_view ProcessFull(std::string_view key, std::string_view value) override {
      if (actual_ != nullptr) {
        *actual_ = value;
      }
      if (expected_.data() != nullptr && expected_ == value) {
        return desired_.data() == nullptr ? REMOVE : desired_;
      }
      status_->Set(Status::INFEASIBLE_ERROR);
      return NOOP;
    }

    /**
     * Processes an empty record space.
     */
    std::string_view ProcessEmpty(std::string_view key) override {
      if (actual_ != nullptr) {
        *actual_ = "";
      }
      if (expected_.data() == nullptr) {
        return desired_.data() == nullptr ? NOOP : desired_;
      }
      status_->Set(Status::INFEASIBLE_ERROR);
      return NOOP;
    }

   private:
    /** Status to report. */
    Status* status_;
    /** The expected value. */
    std::string_view expected_;
    /** The desired value. */
    std::string_view desired_;
    /** Actual value to report. */
    std::string* actual_;
  };

  /**
   * Record checker to implement DBM::CompareExchangeMulti.
   */
  class RecordCheckerCompareExchangeMulti final : public DBM::RecordProcessor {
   public:
    /**
     * Constructor.
     * @param noop Whether to do no operation.
     * @param expected A string of the expected value.
     */
    RecordCheckerCompareExchangeMulti(bool* noop, std::string_view expected)
        : noop_(noop), expected_(expected) {}

    /**
     * Processes an existing record.
     */
    std::string_view ProcessFull(std::string_view key, std::string_view value) override {
      if (expected_.data() == nullptr || expected_ != value) {
        *noop_ = true;
      }
      return NOOP;
    }

    /**
     * Processes an empty record space.
     */
    std::string_view ProcessEmpty(std::string_view key) override {
      if (expected_.data() != nullptr) {
        *noop_ = true;
      }
      return NOOP;
    }

   private:
    /** Whether to do no operation. */
    bool* noop_;
    /** The expected value. */
    std::string_view expected_;
  };

  /**
   * Record setter to implement DBM::CompareExchangeMulti.
   */
  class RecordSetterCompareExchangeMulti final : public DBM::RecordProcessor {
   public:
    /**
     * Constructor.
     * @param noop True to do no operation.
     * @param desired A string of the expected value.
     */
    RecordSetterCompareExchangeMulti(bool* noop, std::string_view desired)
        : noop_(noop), desired_(desired) {}

    /**
     * Processes an existing record.
     */
    std::string_view ProcessFull(std::string_view key, std::string_view value) override {
      if (*noop_) {
        return NOOP;
      }
      return desired_.data() == nullptr ? REMOVE : desired_;
    }

    /**
     * Processes an empty record space.
     */
    std::string_view ProcessEmpty(std::string_view key) override {
      if (*noop_) {
        return NOOP;
      }
      return desired_.data() == nullptr ? NOOP : desired_;
    }

   private:
    /** Whether to do no operation. */
    bool* noop_;
    /** The desired value. */
    std::string_view desired_;
  };

  /**
   * Record processor to implement DBM::Increment.
   */
  class RecordProcessorIncrement final : public DBM::RecordProcessor {
   public:
    /**
     * Constructor.
     * @param increment The incremental value.
     * @param current The pointer to a string object to contain the current value.
     * @param initial The initial value.
     */
    RecordProcessorIncrement(int64_t increment, int64_t* current, int64_t initial)
        : increment_(increment), current_(current), initial_(initial) {}

    /**
     * Processes an existing record.
     */
    std::string_view ProcessFull(std::string_view key, std::string_view value) override {
      if (increment_ == INT64MIN) {
        if (current_ != nullptr) {
          *current_ = StrToIntBigEndian(value);
        }
        return NOOP;
      }
      const int64_t num = StrToIntBigEndian(value) + increment_;
      if (current_ != nullptr) {
        *current_ = num;
      }
      value_ = IntToStrBigEndian(num);
      return value_;
    }

    /**
     * Processes an empty record space.
     */
    std::string_view ProcessEmpty(std::string_view key) override {
      if (increment_ == INT64MIN) {
        if (current_ != nullptr) {
          *current_ = initial_;
        }
        return NOOP;
      }
      const int64_t num = initial_ + increment_;
      if (current_ != nullptr) {
        *current_ = num;
      }
      value_ =  IntToStrBigEndian(num);
      return value_;
    }

   private:
    /** The incrementing value. */
    int64_t increment_;
    /** The current value to report. */
    int64_t* current_;
    /** The initial value. */
    int64_t initial_;
    /** The new string value. */
    std::string value_;
  };

  /**
   * Record processor to implement DBM::Export.
   */
  class RecordProcessorExport final : public RecordProcessor {
   public:
    /**
     * Constructor.
     */
    RecordProcessorExport(Status* status, DBM* dbm) : status_(status), dbm_(dbm) {}

    /**
     * Processes an existing record.
     */
    std::string_view ProcessFull(std::string_view key, std::string_view value) override {
      *status_ |= dbm_->Set(key, value);
      return NOOP;
    }

   private:
    /** Status to report. */
    Status* status_;
    /** Destination database. */
    DBM* dbm_;
  };

  /**
   * Record processor to implement DBM::Iterator::Get.
   */
  class RecordProcessorIteratorGet final : public RecordProcessor {
   public:
    /**
     * Constructor.
     * @param key The pointer to a string object to contain the result key.
     * @param value The pointer to a string object to contain the result value.
     */
    RecordProcessorIteratorGet(std::string* key, std::string* value) : key_(key), value_(value) {}

    /**
     * Processes an existing record.
     */
    std::string_view ProcessFull(std::string_view key, std::string_view value) override {
      if (key_ != nullptr) {
        *key_ = key;
      }
      if (value_ != nullptr) {
        *value_ = value;
      }
      return NOOP;
    }

   private:
    /** Key to report. */
    std::string* key_;
    /** Value to report. */
    std::string* value_;
  };

  /**
   * Record processor to implement DBM::Iterator::Set.
   */
  class RecordProcessorIteratorSet final : public RecordProcessor {
   public:
    /**
     * Constructor.
     * @param value A string of the value to set.
     */
    explicit RecordProcessorIteratorSet(std::string_view value) : value_(value) {}

    /**
     * Processes an existing record.
     */
    std::string_view ProcessFull(std::string_view key, std::string_view value) override {
      return value_;
    }

   private:
    /** Value to store. */
    std::string_view value_;
  };

  /**
   * Record processor to implement DBM::Iterator::Remove.
   */
  class RecordProcessorIteratorRemove final : public RecordProcessor {
   public:
    /**
     * Processes an existing record.
     */
    std::string_view ProcessFull(std::string_view key, std::string_view value) override {
      return REMOVE;
    }
  };

  /**
   * Interface of iterator for each record.
   */
  class Iterator {
   public:
    /**
     * Destructor.
     */
    virtual ~Iterator() = default;

    /**
     * Initializes the iterator to indicate the first record.
     * @return The result status.
     * @details Even if there's no record, the operation doesn't fail.
     */
    virtual Status First() = 0;

    /**
     * Initializes the iterator to indicate the last record.
     * @return The result status.
     * @details Even if there's no record, the operation doesn't fail.  This method is suppoerted
     * only by ordered databases.
     */
    virtual Status Last() = 0;

    /**
     * Initializes the iterator to indicate a specific record.
     * @param key The key of the record to look for.
     * @return The result status.
     * @details Ordered databases can support "lower bound" jump; If there's no record with the
     * same key, the iterator refers to the first record whose key is greater than the given key.
     * The operation fails with unordered databases if there's no record with the same key.
     */
    virtual Status Jump(std::string_view key) = 0;

    /**
     * Initializes the iterator to indicate the last record whose key is lower than a given key.
     * @param key The key to compare with.
     * @param inclusive If true, the condition is inclusive: equal to or lower than the key.
     * @return The result status.
     * @details Even if there's no matching record, the operation doesn't fail.  This method is
     * suppoerted only by ordered databases.
     */
    virtual Status JumpLower(std::string_view key, bool inclusive = false) = 0;

    /**
     * Initializes the iterator to indicate the first record whose key is upper than a given key.
     * @param key The key to compare with.
     * @param inclusive If true, the condition is inclusive: equal to or upper than the key.
     * @return The result status.
     * @details Even if there's no matching record, the operation doesn't fail.  This method is
     * suppoerted only by ordered databases.
     */
    virtual Status JumpUpper(std::string_view key, bool inclusive = false) = 0;

    /**
     * Moves the iterator to the next record.
     * @return The result status.
     * @details If the current record is missing, the operation fails.  Even if there's no next
     * record, the operation doesn't fail.
     */
    virtual Status Next() = 0;

    /**
     * Moves the iterator to the previous record.
     * @return The result status.
     * @details If the current record is missing, the operation fails.  Even if there's no previous
     * record, the operation doesn't fail.  This method is suppoerted only by ordered databases.
     */
    virtual Status Previous() = 0;

    /**
     * Processes the current record with a processor.
     * @param proc The pointer to the processor object.
     * @param writable True if the processor can edit the record.
     * @return The result status.
     * @details If the current record exists, the ProcessFull of the processor is called.
     * Otherwise, this method fails and no method of the processor is called.  If the current
     * record is removed, the iterator is moved to the next record.
     */
    virtual Status Process(RecordProcessor* proc, bool writable) = 0;

    /**
     * Processes the current record with a lambda function.
     * @param rec_lambda The lambda function to process a record.  The first parameter is the key
     * of the record.  The second parameter is the value of the existing record.  The return
     * value is a string reference to NOOP, REMOVE, or the new record value.
     * @param writable True if the processor can edit the record.
     * @return The result status.
     */
    virtual Status Process(RecordLambdaType rec_lambda, bool writable) {
      RecordProcessorLambda proc(rec_lambda);
      return Process(&proc, writable);
    }

    /**
     * Gets the key and the value of the current record of the iterator.
     * @param key The pointer to a string object to contain the record key.  If it is nullptr,
     * the key data is ignored.
     * @param value The pointer to a string object to contain the record value.  If it is nullptr,
     * the value data is ignored.
     * @return The result status.
     */
    virtual Status Get(std::string* key = nullptr, std::string* value = nullptr) {
      RecordProcessorIteratorGet proc(key, value);
      return Process(&proc, false);
    }

    /**
     * Gets the key of the current record, in a simple way.
     * @param default_value The value to be returned on failure.
     * @return The key of the current record on success, or the default value on failure.
     */
    virtual std::string GetKey(std::string_view default_value = "") {
      std::string key;
      return Get(&key, nullptr) == Status::SUCCESS ? key : std::string(default_value);
    }

    /**
     * Gets the value of the current record, in a simple way.
     * @param default_value The value to be returned on failure.
     * @return The value of the current record on success, or the default value on failure.
     */
    virtual std::string GetValue(std::string_view default_value = "") {
      std::string value;
      return Get(nullptr, &value) == Status::SUCCESS ? value : std::string(default_value);
    }

    /**
     * Sets the value of the current record.
     * @param value The value of the record.
     * @return The result status.
     */
    virtual Status Set(std::string_view value) {
      RecordProcessorIteratorSet proc(value);
      return Process(&proc, true);
    }

    /**
     * Removes the current record.
     * @return The result status.
     * @details If possible, the iterator moves to the next record.
     */
    virtual Status Remove() {
      RecordProcessorIteratorRemove proc;
      return Process(&proc, true);
    }
  };

  /**
   * Interface of processor for a record.
   */
  class FileProcessor {
   public:
    /**
     * Destructor.
     */
    virtual ~FileProcessor() = default;

    /**
     * Process a file.
     * @param path The path of the file.
     */
    virtual void Process(const std::string& path) {}
  };

  /**
   * File processor to implement DBM::CopyFileData.
   */
  class FileProcessorCopyFileData : public FileProcessor {
   public:
    /**
     * Constructor.
     * @param status The pointer to a status object to contain the result status.
     * @param dest_path The destination path for copying.
     */
    FileProcessorCopyFileData(Status* status, const std::string dest_path);

    /**
     * Process a file.
     * @param path The path of the file.
     */
    void Process(const std::string& path) override;

   private:
    Status* status_;
    std::string dest_path_;
  };

  /**
   * Destructor.
   */
  virtual ~DBM() = default;

  /**
   * Opens a database file.
   * @param path A path of the file.
   * @param writable If true, the file is writable.  If false, it is read-only.
   * @param options Bit-sum options of File::OpenOption enumss for opening the file.
   * @return The result status.
   */
  virtual Status Open(const std::string& path, bool writable,
                      int32_t options = File::OPEN_DEFAULT) = 0;

  /**
   * Closes the database file.
   * @return The result status.
   */
  virtual Status Close() = 0;

  /**
   * Processes a record with a processor.
   * @param key The key of the record.
   * @param proc The pointer to the processor object.
   * @param writable True if the processor can edit the record.
   * @return The result status.
   * @details If the specified record exists, the ProcessFull of the processor is called.
   * Otherwise, the ProcessEmpty of the processor is called.
   */
  virtual Status Process(std::string_view key, RecordProcessor* proc, bool writable) = 0;

  /**
   * Processes a record with a lambda function.
   * @param key The key of the record.
   * @param rec_lambda The lambda function to process a record.  The first parameter is the key
   * of the record.  The second parameter is the value of the existing record, or NOOP if it the
   * record doesn't exist.  The return value is a string reference to NOOP, REMOVE, or the new
   * record value.
   * @param writable True if the processor can edit the record.
   * @return The result status.
   */
  virtual Status Process(std::string_view key, RecordLambdaType rec_lambda, bool writable) {
    RecordProcessorLambda proc(rec_lambda);
    return Process(key, &proc, writable);
  }

  /**
   * Gets the value of a record of a key.
   * @param key The key of the record.
   * @param value The pointer to a string object to contain the result value.  If it is nullptr,
   * the value data is ignored.
   * @return The result status.  If there's no matching record, NOT_FOUND_ERROR is returned.
   */
  virtual Status Get(std::string_view key, std::string* value = nullptr) {
    Status impl_status(Status::SUCCESS);
    RecordProcessorGet proc(&impl_status, value);
    const Status status = Process(key, &proc, false);
    if (status != Status::SUCCESS) {
      return status;
    }
    return impl_status;
  }

  /**
   * Gets the value of a record of a key, in a simple way.
   * @param key The key of the record.
   * @param default_value The value to be returned on failure.
   * @return The value of the matching record on success, or the default value on failure.
   */
  virtual std::string GetSimple(std::string_view key, std::string_view default_value = "") {
    std::string value;
    return Get(key, &value) == Status::SUCCESS ? value : std::string(default_value);
  }

  /**
   * Gets the values of multiple records of keys, with a string view vector.
   * @param keys The keys of records to retrieve.
   * @param records The pointer to a map to store retrieved records.  Keys which don't match
   * existing records are ignored.
   * @return The result status.  If all records of the given keys are found, SUCCESS is returned.
   * If one or more records are missing, NOT_FOUND_ERROR is returned.  Thus, even with an error
   * code, the result map can have elements.
   */
  virtual Status GetMulti(
      const std::vector<std::string_view>& keys, std::map<std::string, std::string>* records) {
    Status status(Status::SUCCESS);
    for (const auto& key : keys) {
      std::string value;
      const Status tmp_status = Get(key, &value);
      if (tmp_status == Status::SUCCESS) {
        records->emplace(key, std::move(value));
      } else {
        status |= tmp_status;
      }
    }
    return status;
  }

  /**
   * Gets the values of multiple records of keys, with an initializer list.
   * @param keys The keys of records to retrieve.
   * @param records The pointer to a map to store retrieved records.  Keys which don't match
   * existing records are ignored.
   * @return The result status.  If all records of the given keys are found, SUCCESS is returned.
   * If one or more records are missing, NOT_FOUND_ERROR is returned.  Thus, even with an error
   * code, the result map can have elements.
   */
  virtual Status GetMulti(const std::initializer_list<std::string_view>& keys,
                          std::map<std::string, std::string>* records) {
    std::vector<std::string_view> vector_keys(keys.begin(), keys.end());
    return GetMulti(vector_keys, records);
  }

  /**
   * Gets the values of multiple records of keys, with a string vector.
   * @param keys The keys of records to retrieve.
   * @param records The pointer to a map to store retrieved records.  Keys which don't match
   * existing records are ignored.
   * @return The result status.  If all records of the given keys are found, SUCCESS is returned.
   * If one or more records are missing, NOT_FOUND_ERROR is returned.  Thus, even with an error
   * code, the result map can have elements.
   */
  virtual Status GetMulti(
      const std::vector<std::string>& keys, std::map<std::string, std::string>* records) {
    return GetMulti(MakeStrViewVectorFromValues(keys), records);
  }

  /**
   * Sets a record of a key and a value.
   * @param key The key of the record.
   * @param value The value of the record.
   * @param overwrite Whether to overwrite the existing value if there's a record with the same
   * key.  If true, the existing value is overwritten by the new value.  If false, the operation
   * is given up and an error status is returned.
   * @param old_value The pointer to a string object to contain the old value.  Assignment is done
   * even on the duplication error.  If it is nullptr, it is ignored.
   * @return The result status.  If overwriting is abandoned, DUPLICATION_ERROR is returned.
   */
  virtual Status Set(std::string_view key, std::string_view value, bool overwrite = true,
                     std::string* old_value = nullptr) {
    Status impl_status(Status::SUCCESS);
    RecordProcessorSet proc(&impl_status, value, overwrite, old_value);
    const Status status = Process(key, &proc, true);
    if (status != Status::SUCCESS) {
      return status;
    }
    return impl_status;
  }

  /**
   * Sets multiple records, with a map of string views.
   * @param records The records to store.
   * @param overwrite Whether to overwrite the existing value if there's a record with the same
   * key.  If true, the existing value is overwritten by the new value.  If false, the operation
   * is given up and an error status is returned.
   * @return The result status.  If there are records avoiding overwriting, DUPLICATION_ERROR
   * is returned.
   */
  virtual Status SetMulti(
      const std::map<std::string_view, std::string_view>& records, bool overwrite = true) {
    Status status(Status::SUCCESS);
    for (const auto& record : records) {
      status |= Set(record.first, record.second, overwrite);
      if (status != Status::SUCCESS && status != Status::DUPLICATION_ERROR) {
        break;
      }
    }
    return status;
  }

  /**
   * Sets multiple records, with an initializer list.
   * @param records The records to store.
   * @param overwrite Whether to overwrite the existing value if there's a record with the same
   * key.  If true, the existing value is overwritten by the new value.  If false, the operation
   * is given up and an error status is returned.
   * @return The result status.  If there are records avoiding overwriting, DUPLICATION_ERROR
   * is returned.
   */
  virtual Status SetMulti(
      const std::initializer_list<std::pair<std::string_view, std::string_view>>& records,
      bool overwrite = true) {
    std::map<std::string_view, std::string_view> map_records;
    for (const auto& record : records) {
      map_records.emplace(std::pair(
          std::string_view(record.first), std::string_view(record.second)));
    }
    return SetMulti(map_records, overwrite);
  }

  /**
   * Sets multiple records, with a map of strings.
   * @param records The records to store.
   * @param overwrite Whether to overwrite the existing value if there's a record with the same
   * key.  If true, the existing value is overwritten by the new value.  If false, the operation
   * is given up and an error status is returned.
   * @return The result status.  If there are records avoiding overwriting, DUPLICATION_ERROR
   * is returned.
   */
  virtual Status SetMulti(
      const std::map<std::string, std::string>& records, bool overwrite = true) {
    return SetMulti(MakeStrViewMapFromRecords(records), overwrite);
  }

  /**
   * Removes a record of a key.
   * @param key The key of the record.
   * @param old_value The pointer to a string object to contain the old value.  If it is nullptr,
   * it is ignored.
   * @return The result status.  If there's no matching record, NOT_FOUND_ERROR is returned.
   */
  virtual Status Remove(std::string_view key, std::string* old_value = nullptr) {
    Status impl_status(Status::SUCCESS);
    RecordProcessorRemove proc(&impl_status, old_value);
    const Status status = Process(key, &proc, true);
    if (status != Status::SUCCESS) {
      return status;
    }
    return impl_status;
  }

  /**
   * Removes records of keys, with a string view vector.
   * @param keys The keys of records to remove.
   * @return The result status.  If there are missing records, NOT_FOUND_ERROR is returned.
   */
  virtual Status RemoveMulti(const std::vector<std::string_view>& keys) {
    Status status(Status::SUCCESS);
    for (const auto& key : keys) {
      status |= Remove(key);
      if (status != Status::Status::SUCCESS && status != Status::Status::NOT_FOUND_ERROR) {
        break;
      }
    }
    return status;
  }

  /**
   * Removes records of keys, with an initializer list.
   * @param keys The keys of records to remove.
   * @return The result status.
   */
  virtual Status RemoveMulti(const std::initializer_list<std::string_view>& keys) {
    std::vector<std::string_view> vector_keys(keys.begin(), keys.end());
    return RemoveMulti(vector_keys);
  }

  /**
   * Removes records of keys, with a string vector.
   * @param keys The keys of records to remove.
   * @return The result status.  If there are missing records, NOT_FOUND_ERROR is returned.
   */
  virtual Status RemoveMulti(const std::vector<std::string>& keys) {
    return RemoveMulti(MakeStrViewVectorFromValues(keys));
  }

  /**
   * Appends data at the end of a record of a key.
   * @param key The key of the record.
   * @param value The value to append.
   * @param delim The delimiter to put after the existing record.
   * @return The result status.
   * @details If there's no existing record, the value is set without the delimiter.
   */
  virtual Status Append(
      std::string_view key, std::string_view value, std::string_view delim = "") {
    RecordProcessorAppend proc(value, delim);
    return Process(key, &proc, true);
  }

  /**
   * Appends data to multiple records, with a map of string views.
   * @param records The records to append.
   * @param delim The delimiter to put after the existing record.
   * @return The result status.
   * @details If there's no existing record, the value is set without the delimiter.
   */
  virtual Status AppendMulti(
      const std::map<std::string_view, std::string_view>& records, std::string_view delim = "") {
    Status status(Status::SUCCESS);
    for (const auto& record : records) {
      status |= Append(record.first, record.second, delim);
      if (status != Status::SUCCESS) {
        break;
      }
    }
    return status;
  }

  /**
   * Appends data to multiple records, with an initializer list.
   * @param records The records to store.
   * @param delim The delimiter to put after the existing record.
   * @return The result status.
   * @details If there's no existing record, the value is set without the delimiter.
   */
  virtual Status AppendMulti(
      const std::initializer_list<std::pair<std::string_view, std::string_view>>& records,
      std::string_view delim = "") {
    std::map<std::string_view, std::string_view> map_records;
    for (const auto& record : records) {
      map_records.emplace(std::pair(
          std::string_view(record.first), std::string_view(record.second)));
    }
    return AppendMulti(map_records, delim);
  }

  /**
   * Appends data to multiple records, with a map of strings.
   * @param records The records to append.
   * @param delim The delimiter to put after the existing record.
   * @return The result status.
   * @details If there's no existing record, the value is set without the delimiter.
   */
  virtual Status AppendMulti(
      const std::map<std::string, std::string>& records, std::string_view delim = "") {
    return AppendMulti(MakeStrViewMapFromRecords(records), delim);
  }

  /**
   * Compares the value of a record and exchanges if the condition meets.
   * @param key The key of the record.
   * @param expected The expected value.  If the data is nullptr, no existing record is expected.
   * @param desired The desired value.  If the data is nullptr, the record is to be removed.
   * @param actual The pointer to a string object to contain the result value.  If it is nullptr,
   * it is ignored.
   * @return The result status.  If the condition doesn't meet, INFEASIBLE_ERROR is returned.
   */
  virtual Status CompareExchange(std::string_view key, std::string_view expected,
                                 std::string_view desired, std::string* actual = nullptr) {
    Status impl_status(Status::SUCCESS);
    RecordProcessorCompareExchange proc(&impl_status, expected, desired, actual);
    const Status status = Process(key, &proc, true);
    if (status != Status::SUCCESS) {
      return status;
    }
    return impl_status;
  }

  /**
   * Increments the numeric value of a record.
   * @param key The key of the record.
   * @param increment The incremental value.  If it is INT64MIN, the current value is not changed
   * and a new record is not created.
   * @param current The pointer to an integer to contain the current value.  If it is nullptr,
   * it is ignored.
   * @param initial The initial value.
   * @return The result status.
   * @details The record value is stored as an 8-byte big-endian integer.  Negative is also
   * supported.
   */
  virtual Status Increment(std::string_view key, int64_t increment = 1,
                           int64_t* current = nullptr, int64_t initial = 0) {
    RecordProcessorIncrement proc(increment, current, initial);
    return Process(key, &proc, increment != INT64MIN);
  }

  /**
   * Increments the numeric value of a record, in a simple way.
   * @param key The key of the record.
   * @param increment The incremental value.
   * @param initial The initial value.
   * @return The current value or INT64MIN on failure.
   * @details The record value is treated as a decimal integer.  Negative is also supported.
   */
  int64_t IncrementSimple(std::string_view key, int64_t increment = 1, int64_t initial = 0) {
    int64_t current = 0;
    return Increment(key, increment, &current, initial) == Status::SUCCESS ? current : INT64MIN;
  }

  /**
   * Processes multiple records with processors.
   * @param key_proc_pairs Pairs of the keys and their processor objects.
   * @param writable True if the processors can edit the records.
   * @return The result status.
   * @details If the specified record exists, the ProcessFull of the processor is called.
   * Otherwise, the ProcessEmpty of the processor is called.
   */
  virtual Status ProcessMulti(
      const std::vector<std::pair<std::string_view, RecordProcessor*>>& key_proc_pairs,
      bool writable) = 0;

  /**
   * Processes multiple records with lambda functions.
   * @param key_lambda_pairs Pairs of the keys and their lambda functions.  The first parameter of
   * the lambda functions is the key of the record, or NOOP if it the record doesn't exist.  The
   * return value is a string reference to NOOP, REMOVE, or the new record value.
   * @param writable True if the processors can edit the records.
   * @return The result status.
   */
  virtual Status ProcessMulti(
      const std::vector<std::pair<std::string_view, RecordLambdaType>>& key_lambda_pairs,
      bool writable) {
    std::vector<std::pair<std::string_view, RecordProcessor*>> key_proc_pairs;
    key_proc_pairs.reserve(key_lambda_pairs.size());
    std::vector<RecordProcessorLambda> procs;
    procs.reserve(key_lambda_pairs.size());
    for (const auto& key_lambda : key_lambda_pairs) {
      procs.emplace_back(key_lambda.second);
      key_proc_pairs.emplace_back(std::make_pair(key_lambda.first, &procs.back()));
    }
    return ProcessMulti(key_proc_pairs, writable);
  }

  /**
   * Compares the values of records and exchanges if the condition meets.
   * @param expected The record keys and their expected values.  If the value is nullptr, no
   * existing record is expected.
   * @param desired The record keys and their desired values.  If the value is nullptr, the
   * record is to be removed.
   * @return The result status.  If the condition doesn't meet, INFEASIBLE_ERROR is returned.
   */
  virtual Status CompareExchangeMulti(
      const std::vector<std::pair<std::string_view, std::string_view>>& expected,
      const std::vector<std::pair<std::string_view, std::string_view>>& desired) {
    std::vector<std::pair<std::string_view, RecordProcessor*>> key_proc_pairs;
    key_proc_pairs.reserve(expected.size() + desired.size());
    bool noop = false;
    std::vector<RecordCheckerCompareExchangeMulti> checkers;
    checkers.reserve(expected.size());
    for (const auto& key_value : expected) {
      checkers.emplace_back(RecordCheckerCompareExchangeMulti(&noop, key_value.second));
      key_proc_pairs.emplace_back(std::pair(key_value.first, &checkers.back()));
    }
    std::vector<RecordSetterCompareExchangeMulti> setters;
    setters.reserve(desired.size());
    for (const auto& key_value : desired) {
      setters.emplace_back(RecordSetterCompareExchangeMulti(&noop, key_value.second));
      key_proc_pairs.emplace_back(std::pair(key_value.first, &setters.back()));
    }
    const Status status = ProcessMulti(key_proc_pairs, true);
    if (status != Status::SUCCESS) {
      return status;
    }
    return noop ? Status(Status::INFEASIBLE_ERROR) : Status(Status::SUCCESS);
  }

  /**
   * Processes each and every record in the database with a processor.
   * @param proc The pointer to the processor object.
   * @param writable True if the processor can edit the record.
   * @return The result status.
   * @details The ProcessFull of the processor is called repeatedly for each record.  The
   * ProcessEmpty of the processor is called once before the iteration and once after the
   * iteration.
   */
  virtual Status ProcessEach(RecordProcessor* proc, bool writable) = 0;

  /**
   * Processes each and every record in the database with a lambda function.
   * @param rec_lambda The lambda function to process a record.  The first parameter is the key
   * of the record.  The second parameter is the value of the existing record, or NOOP if it the
   * record doesn't exist.  The return value is a string reference to NOOP, REMOVE, or the new
   * record value.
   * @param writable True if the processor can edit the record.
   * @return The result status.
   * @details The lambda function is called repeatedly for each record.  It is also called once
   * before the iteration and once after the iteration with both the key and the value being NOOP.
   */
  virtual Status ProcessEach(RecordLambdaType rec_lambda, bool writable) {
    RecordProcessorLambda proc(rec_lambda);
    return ProcessEach(&proc, writable);
  }

  /**
   * Gets the number of records.
   * @param count The pointer to an integer object to contain the result count.
   * @return The result status.
   */
  virtual Status Count(int64_t* count) = 0;

  /**
   * Gets the number of records, in a simple way.
   * @return The number of records on success, or -1 on failure.
   */
  virtual int64_t CountSimple() {
    int64_t count = 0;
    return Count(&count) == Status::SUCCESS ? count : -1;
  }

  /**
   * Gets the current file size of the database.
   * @param size The pointer to an integer object to contain the result size.
   * @return The result status.
   */
  virtual Status GetFileSize(int64_t* size) = 0;

  /**
   * Gets the current file size of the database, in a simple way.
   * @return The current file size of the database, or -1 on failure.
   */
  virtual int64_t GetFileSizeSimple() {
    int64_t size = 0;
    return GetFileSize(&size) == Status::SUCCESS ? size : -1;
  }

  /**
   * Gets the path of the database file.
   * @param path The pointer to a string object to contain the result path.
   * @return The result status.
   */
  virtual Status GetFilePath(std::string* path) = 0;

  /**
   * Gets the path of the database file, in a simple way.
   * @return The file path of the database, or an empty string on failure.
   */
  virtual std::string GetFilePathSimple() {
    std::string path;
    return GetFilePath(&path) == Status::SUCCESS ? path : std::string("");
  }

  /**
   * Removes all records.
   * @return The result status.
   */
  virtual Status Clear() = 0;

  /**
   * Rebuilds the entire database.
   * @return The result status.
   */
  virtual Status Rebuild() = 0;

  /**
   * Checks whether the database should be rebuilt.
   * @param tobe The pointer to a boolean object to contain the result decision.
   * @return The result status.
   */
  virtual Status ShouldBeRebuilt(bool* tobe) = 0;

  /**
   * Checks whether the database should be rebuilt, in a simple way.
   * @return True if the database should be rebuilt or false if not or on failure.
   */
  virtual bool ShouldBeRebuiltSimple() {
    bool tobe = false;
    return ShouldBeRebuilt(&tobe) == Status::SUCCESS ? tobe : false;
  }

  /**
   * Synchronizes the content of the database to the file system.
   * @param hard True to do physical synchronization with the hardware or false to do only
   * logical synchronization with the file system.
   * @param proc The pointer to the file processor object, whose Process method is called while
   * the content of the file is synchronized.  If it is nullptr, it is ignored.
   * @return The result status.
   */
  virtual Status Synchronize(bool hard, FileProcessor* proc = nullptr) = 0;

  /**
   * Copies the content of the database file to another file.
   * @param dest_path A path to the destination file.
   * @return The result status.
   * @details Copying is done while the content is synchronized and stable.  So, this method is
   * suitable for making a backup file while running a database service.
   */
  virtual Status CopyFileData(const std::string& dest_path) {
    Status impl_status(Status::SUCCESS);
    FileProcessorCopyFileData proc(&impl_status, dest_path);
    if (IsWritable()) {
      const Status status = Synchronize(false, &proc);
      if (status != Status::SUCCESS) {
        return status;
      }
    } else {
      std::string path;
      const Status status = GetFilePath(&path);
      if (status != Status::SUCCESS) {
        return status;
      }
      proc.Process(path);
    }
    return impl_status;
  }

  /**
   * Exports all records to another database.
   * @param dest_dbm The pointer to the destination database.
   * @return The result status.
   */
  virtual Status Export(DBM* dest_dbm) {
    Status impl_status(Status::SUCCESS);
    RecordProcessorExport proc(&impl_status, dest_dbm);
    const Status status = ProcessEach(&proc, false);
    if (status != Status::SUCCESS) {
      return status;
    }
    return impl_status;
  }

  /**
   * Inspects the database.
   * @return A vector of pairs of a property name and its value.
   */
  virtual std::vector<std::pair<std::string, std::string>> Inspect() = 0;

  /**
   * Checks whether the database is open.
   * @return True if the database is open, or false if not.
   */
  virtual bool IsOpen() const = 0;

  /**
   * Checks whether the database is writable.
   * @return True if the database is writable, or false if not.
   */
  virtual bool IsWritable() const = 0;

  /**
   * Checks whether the database condition is healthy.
   * @return True if the database condition is healthy, or false if not.
   */
  virtual bool IsHealthy() const = 0;

  /**
   * Checks whether ordered operations are supported.
   * @return True if ordered operations are supported, or false if not.
   */
  virtual bool IsOrdered() const = 0;

  /**
   * Makes an iterator for each record.
   * @return The iterator for each record.
   */
  virtual std::unique_ptr<Iterator> MakeIterator() = 0;

  /**
   * Makes a new DBM object of the same concrete class.
   * @return The new DBM object.
   */
  virtual std::unique_ptr<DBM> MakeDBM() const = 0;

  /**
   * Gets the type information of the actual class.
   * @return The type information of the actual class.
   */
  const std::type_info& GetType() const {
    const auto& entity = *this;
    return typeid(entity);
  }
};

}  // namespace tkrzw

#endif  // _TKRZW_DBM_H

// END OF FILE
