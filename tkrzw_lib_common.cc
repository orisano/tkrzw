/*************************************************************************************************
 * Common library features
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

#include "tkrzw_sys_config.h"

#include "tkrzw_lib_common.h"

namespace tkrzw {

#if defined(_SYS_WINDOWS_)

const int32_t PAGE_SIZE = 4096;
const char* const PACKAGE_VERSION = _TKRZW_PKG_VERSION;;
const char* const LIBRARY_VERSION = _TKRZW_LIB_VERSION;;
const char* const OS_NAME = _TKRZW_OSNAME;
const bool IS_POSIX = _IS_POSIX;
const bool IS_BIG_ENDIAN = _IS_BIG_ENDIAN;
constexpr int32_t EDQUOT = 10001;

#else

const int32_t PAGE_SIZE = sysconf(_SC_PAGESIZE);
const char* const PACKAGE_VERSION = _TKRZW_PKG_VERSION;;
const char* const LIBRARY_VERSION = _TKRZW_LIB_VERSION;;
const char* const OS_NAME = _TKRZW_OSNAME;
const bool IS_POSIX = _IS_POSIX;
const bool IS_BIG_ENDIAN = _IS_BIG_ENDIAN;

#endif

void* xmallocaligned(size_t alignment, size_t size) {
#if defined(_SYS_LINUX_)
  assert(alignment > 0);
  void* ptr = std::aligned_alloc(alignment, size);
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  return ptr;
#elif defined(_SYS_WINDOWS_)
  assert(alignment > 0);
  void* ptr = _aligned_malloc(size, alignment);
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  return ptr;
#else
  assert(alignment > 0);
  void* ptr = xmalloc(size + sizeof(void*) + alignment);
  char* aligned = (char*)ptr + sizeof(void*);
  aligned += alignment - (intptr_t)aligned % alignment;
  std::memcpy(aligned - sizeof(void*), &ptr, sizeof(void*));
  return aligned;
#endif
}

void xfreealigned(void* ptr) {
#if defined(_SYS_LINUX_)
  assert(ptr != nullptr);
  std::free(ptr);
#elif defined(_SYS_WINDOWS_)
  assert(ptr != nullptr);
  _aligned_free(ptr);
#else
  assert(ptr != nullptr);
  void* orig = nullptr;
  std::memcpy(&orig, (char*)ptr - sizeof(void*), sizeof(void*));
  std::free(orig);
#endif
}

uint64_t HashMurmur(const void* buf, size_t size, uint64_t seed) {
  assert(buf != nullptr && size <= (1 << 30));
  const uint64_t mul = 0xc6a4a7935bd1e995ULL;
  const int32_t rtt = 47;
  uint64_t hash = seed ^ (size * mul);
  const unsigned char* rp = (const unsigned char*)buf;
  while (size >= sizeof(uint64_t)) {
    uint64_t num = ((uint64_t)rp[0] << 0) | ((uint64_t)rp[1] << 8) |
        ((uint64_t)rp[2] << 16) | ((uint64_t)rp[3] << 24) |
        ((uint64_t)rp[4] << 32) | ((uint64_t)rp[5] << 40) |
        ((uint64_t)rp[6] << 48) | ((uint64_t)rp[7] << 56);
    num *= mul;
    num ^= num >> rtt;
    num *= mul;
    hash *= mul;
    hash ^= num;
    rp += sizeof(uint64_t);
    size -= sizeof(uint64_t);
  }
  switch (size) {
    case 7: hash ^= (uint64_t)rp[6] << 48;  // fall through
    case 6: hash ^= (uint64_t)rp[5] << 40;  // fall through
    case 5: hash ^= (uint64_t)rp[4] << 32;  // fall through
    case 4: hash ^= (uint64_t)rp[3] << 24;  // fall through
    case 3: hash ^= (uint64_t)rp[2] << 16;  // fall through
    case 2: hash ^= (uint64_t)rp[1] << 8;   // fall through
    case 1: hash ^= (uint64_t)rp[0]; hash *= mul;  // fall through
  };
  hash ^= hash >> rtt;
  hash *= mul;
  hash ^= hash >> rtt;
  return hash;
}

uint64_t HashFNV(const void* buf, size_t size) {
  assert(buf != nullptr && size <= (1 << 30));
  uint64_t hash = 14695981039346656037ULL;
  const unsigned char* rp = (unsigned char*)buf;
  const unsigned char* ep = rp + size;
  while (rp < ep) {
    hash = (hash ^ *(rp++)) * 109951162811ULL;
  }
  return hash;
}

uint32_t HashCRC8Continuous(const void* buf, size_t size, bool finish, uint32_t seed) {
  static std::unique_ptr<uint8_t[]> uniq_table;
  static uint8_t* table = nullptr;
  static std::once_flag table_once_flag;
  std::call_once(table_once_flag, [&]() {
    uniq_table.reset(new uint8_t[256]);
    table = uniq_table.get();
    for (uint32_t i = 0; i < 256; i++) {
      uint8_t c = i;
      for (uint32_t j = 0; j < 8; j++) {
        c = (c << 1) ^ ((c & 0x80) ? 0x07 : 0);
      }
      table[i] = c;
    }
  });
  const uint8_t* rp = (uint8_t*)buf;
  const uint8_t* ep = rp + size;
  uint32_t crc = seed;
  while (rp < ep) {
    crc = table[(crc ^ *rp)];
    rp++;
  }
  return crc;
}

uint32_t HashCRC16Continuous(const void* buf, size_t size, bool finish, uint32_t seed) {
  static std::unique_ptr<uint16_t[]> uniq_table;
  static uint16_t* table = nullptr;
  static std::once_flag table_once_flag;
  std::call_once(table_once_flag, [&]() {
    uniq_table.reset(new uint16_t[256]);
    table = uniq_table.get();
    for (uint32_t i = 0; i < 256; i++) {
      uint16_t c = i << 8;
      for (uint32_t j = 0; j < 8; j++) {
        c = (c & 0x8000) ? (0x1021 ^ (c << 1)) : (c << 1);
      }
      table[i] = c;
    }
  });
  const uint8_t* rp = (uint8_t*)buf;
  const uint8_t* ep = rp + size;
  uint32_t crc = seed;
  while (rp < ep) {
    crc = table[((crc >> 8) ^ *rp) & 0xFF] ^ (crc << 8);
    rp++;
  }
  return crc & 0xFFFF;
}

uint32_t HashCRC32Continuous(const void* buf, size_t size, bool finish, uint32_t seed) {
  static std::unique_ptr<uint32_t[]> uniq_table;
  static uint32_t* table = nullptr;
  static std::once_flag table_once_flag;
  std::call_once(table_once_flag, [&]() {
    uniq_table.reset(new uint32_t[256]);
    table = uniq_table.get();
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int32_t j = 0; j < 8; j++) {
        c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
      }
      table[i] = c;
    }
  });
  const uint8_t* rp = (uint8_t*)buf;
  const uint8_t* ep = rp + size;
  uint32_t crc = seed;
  while (rp < ep) {
    crc = table[(crc ^ *rp) & 0xFF] ^ (crc >> 8);
    rp++;
  }
  if (finish) {
    crc ^= 0xFFFFFFFF;
  }
  return crc;
}

const Status& Status::OrDie() const {
  if (code_ != SUCCESS) {
    throw StatusException(*this);
  }
  return *this;
}

Status GetErrnoStatus(const char* call_name, int32_t sys_err_num) {
  auto msg = [&](const char* message) {
    return std::string(call_name) + ": " + message;
  };
  switch (sys_err_num) {
    case EAGAIN: return Status(Status::SYSTEM_ERROR, msg("temporarily unavailable"));
    case EINTR: return Status(Status::SYSTEM_ERROR, msg("interrupted by a signal"));
    case EACCES: return Status(Status::PERMISSION_ERROR, msg("permission denied"));
    case ENOENT: return Status(Status::NOT_FOUND_ERROR, msg("no such file"));
    case ENOTDIR: return Status(Status::NOT_FOUND_ERROR, msg("not a directory"));
    case EISDIR: return Status(Status::INFEASIBLE_ERROR, msg("duplicated directory"));
    case ELOOP: return Status(Status::INFEASIBLE_ERROR, msg("looped path"));
    case EFBIG: return Status(Status::INFEASIBLE_ERROR, msg("too big file"));
    case ENOSPC: return Status(Status::INFEASIBLE_ERROR, msg("no enough space"));
    case ENOMEM: return Status(Status::INFEASIBLE_ERROR, msg("no enough memory"));
    case EEXIST: return Status(Status::DUPLICATION_ERROR, msg("already exist"));
    case ENOTEMPTY: return Status(Status::INFEASIBLE_ERROR, msg("not empty"));
    case EXDEV: return Status(Status::INFEASIBLE_ERROR, msg("cross device move"));
    case EBADF: return Status(Status::SYSTEM_ERROR, msg("bad file descriptor"));
    case EINVAL: return Status(Status::SYSTEM_ERROR, msg("invalid file descriptor"));
    case EIO: return Status(Status::SYSTEM_ERROR, msg("low-level I/O error"));
    case EFAULT: return Status(Status::SYSTEM_ERROR, msg("fault buffer address"));
    case EDQUOT: return Status(Status::INFEASIBLE_ERROR, msg("exhausted quota"));
    case EMFILE: return Status(Status::INFEASIBLE_ERROR, msg("exceeding process limit"));
    case ENFILE: return Status(Status::INFEASIBLE_ERROR, msg("exceeding system-wide limit"));
    case ENAMETOOLONG: return Status(Status::INFEASIBLE_ERROR, msg("too long name"));
    case ETXTBSY: return Status(Status::INFEASIBLE_ERROR, msg("busy file"));
    case EOVERFLOW: return Status(Status::INFEASIBLE_ERROR, msg("size overflow"));
  }
  return Status(Status::SYSTEM_ERROR, msg("unknown error: ") + std::to_string(sys_err_num));
}

}  // namespace tkrzw

// END OF FILE
