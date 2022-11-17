//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_table_bucket_page.cpp
//
// Identification: src/storage/page/hash_table_bucket_page.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/page/hash_table_bucket_page.h"
#include "common/logger.h"
#include "common/util/hash_util.h"
#include "storage/index/generic_key.h"
#include "storage/index/hash_comparator.h"
#include "storage/table/tmp_tuple.h"

namespace bustub {

template <typename KeyType, typename ValueType, typename KeyComparator>
bool HASH_TABLE_BUCKET_TYPE::GetValue(KeyType key, KeyComparator cmp, std::vector<ValueType> *result) {
  for (uint32_t bucket_idx = 0; bucket_idx < BUCKET_ARRAY_SIZE; bucket_idx++) {
    auto now_key = array_[bucket_idx].first;
    if (IsReadable(bucket_idx) && cmp(now_key, key) == 0) {
      result->push_back(array_[bucket_idx].second);
    }
  }
  return !static_cast<bool>(result->empty());
}

template <typename KeyType, typename ValueType, typename KeyComparator>
bool HASH_TABLE_BUCKET_TYPE::Insert(KeyType key, ValueType value, KeyComparator cmp) {
  if (IsFull()) {
    return false;
  }
  std::vector<ValueType> result;
  GetValue(key, cmp, &result);  // 找到所有value为key的值
  // if (std::count(result.begin(), result.end(), value) != 0) { // 查看result中是否有值与value相同
  //   std::cout << "debug111" << std::endl;
  //   return false;
  // }
  if (std::find(result.cbegin(), result.cend(), value) != result.cend()) {
    return false;
  }

  for (uint32_t i = 0; i < BUCKET_ARRAY_SIZE; ++i) {  // 找到可以插入的位置insert
    if (!IsReadable(i)) {
      array_[i] = MappingType(key, value);
      SetOccupied(i);
      SetReadable(i);
      break;
    }
  }
  return true;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
bool HASH_TABLE_BUCKET_TYPE::Remove(KeyType key, ValueType value, KeyComparator cmp) {
  // 删除即：把对应的readable对应位置为0
  for (uint32_t bucket_idx = 0; bucket_idx < BUCKET_ARRAY_SIZE; ++bucket_idx) {
    if (!IsOccupied(bucket_idx)) {
      break;
    }
    //    bool flag = IsReadable(bucket_idx) && cmp(KeyAt(bucket_idx), key) == 0 && ValueAt(bucket_idx) == value;
    if (IsReadable(bucket_idx) && cmp(KeyAt(bucket_idx), key) == 0 && ValueAt(bucket_idx) == value) {
      // 找到在第几个字节处
      char &c = readable_[bucket_idx / 8];
      // 找到在某个字节的某个位置，参考位示图
      uint32_t pos = bucket_idx % 8;
      // c ^= ~(1 << (7 - pos))
      c &= ~(1 << (7 - pos));
      return true;
    }
  }
  return false;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
KeyType HASH_TABLE_BUCKET_TYPE::KeyAt(uint32_t bucket_idx) const {
  return array_[bucket_idx].first;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
ValueType HASH_TABLE_BUCKET_TYPE::ValueAt(uint32_t bucket_idx) const {
  return array_[bucket_idx].second;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void HASH_TABLE_BUCKET_TYPE::RemoveAt(uint32_t bucket_idx) {
  char &c = readable_[bucket_idx / 8];
  uint32_t pos = bucket_idx % 8;
  c &= ~(1 << (7 - pos));
}

template <typename KeyType, typename ValueType, typename KeyComparator>
bool HASH_TABLE_BUCKET_TYPE::IsOccupied(uint32_t bucket_idx) const {
  // 判断对应位是否为1
  int x = static_cast<int>(occupied_[bucket_idx / 8]);
  uint32_t shift_bit = bucket_idx % 8;
  auto res = x >> (7 - shift_bit);
  // (occupied_[bucket_idx / 8] & (1 << (7 - (bucket_idx % 8)))) != 0
  return (res & 0x1) == 0x1;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void HASH_TABLE_BUCKET_TYPE::SetOccupied(uint32_t bucket_idx) {
  // 把对应位置为1
  char x = static_cast<char>(occupied_[bucket_idx / 8]);
  uint32_t shift_bit = bucket_idx % 8;
  occupied_[bucket_idx / 8] = (0x1 << (7 - shift_bit)) | x;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
bool HASH_TABLE_BUCKET_TYPE::IsReadable(uint32_t bucket_idx) const {
  int x = static_cast<int>(readable_[bucket_idx / 8]);
  uint32_t shift_bit = bucket_idx % 8;
  auto res = x >> (7 - shift_bit);
  return (res & 0x1) == 0x1;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void HASH_TABLE_BUCKET_TYPE::SetReadable(uint32_t bucket_idx) {
  char x = static_cast<char>(readable_[bucket_idx / 8]);
  uint32_t shift_bit = bucket_idx % 8;
  readable_[bucket_idx / 8] = (0x1 << (7 - shift_bit)) | x;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
bool HASH_TABLE_BUCKET_TYPE::IsFull() {
  return NumReadable() == BUCKET_ARRAY_SIZE;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
uint32_t HASH_TABLE_BUCKET_TYPE::NumReadable() {
  uint32_t count = 0;
  for (uint32_t i = 0; i < BUCKET_ARRAY_SIZE; ++i) {
    if (!IsOccupied(i)) {
      // 扫描到第一个没有占用过的地方停止
      break;
    }
    if (IsReadable(i)) {
      count++;
    }
  }
  return count;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
bool HASH_TABLE_BUCKET_TYPE::IsEmpty() {
  return NumReadable() == 0;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void HASH_TABLE_BUCKET_TYPE::PrintBucket() {
  uint32_t size = 0;
  uint32_t taken = 0;
  uint32_t free = 0;
  for (size_t bucket_idx = 0; bucket_idx < BUCKET_ARRAY_SIZE; bucket_idx++) {
    if (!IsOccupied(bucket_idx)) {
      break;
    }

    size++;

    if (IsReadable(bucket_idx)) {
      taken++;
    } else {
      free++;
    }
  }

  LOG_INFO("Bucket Capacity: %lu, Size: %u, Taken: %u, Free: %u", BUCKET_ARRAY_SIZE, size, taken, free);
}

// DO NOT REMOVE ANYTHING BELOW THIS LINE
template class HashTableBucketPage<int, int, IntComparator>;

template class HashTableBucketPage<GenericKey<4>, RID, GenericComparator<4>>;
template class HashTableBucketPage<GenericKey<8>, RID, GenericComparator<8>>;
template class HashTableBucketPage<GenericKey<16>, RID, GenericComparator<16>>;
template class HashTableBucketPage<GenericKey<32>, RID, GenericComparator<32>>;
template class HashTableBucketPage<GenericKey<64>, RID, GenericComparator<64>>;

// template class HashTableBucketPage<hash_t, TmpTuple, HashComparator>;

}  // namespace bustub
