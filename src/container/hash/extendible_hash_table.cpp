//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// extendible_hash_table.cpp
//
// Identification: src/container/hash/extendible_hash_table.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "common/exception.h"
#include "common/logger.h"
#include "common/rid.h"
#include "container/hash/extendible_hash_table.h"

namespace bustub {

template <typename KeyType, typename ValueType, typename KeyComparator>
HASH_TABLE_TYPE::ExtendibleHashTable(const std::string &name, BufferPoolManager *buffer_pool_manager,
                                     const KeyComparator &comparator, HashFunction<KeyType> hash_fn)
    : buffer_pool_manager_(buffer_pool_manager), comparator_(comparator), hash_fn_(std::move(hash_fn)) {
  //  implement me!
  //  申请一个目录页
  Page *page = buffer_pool_manager->NewPage(&directory_page_id_);  // 此时directory_page_id_已经被赋值
  HashTableDirectoryPage *dir_page = reinterpret_cast<HashTableDirectoryPage *>(page->GetData());
  dir_page->SetPageId(directory_page_id_);

  //  申请一个桶页面
  page_id_t bucket_page_id;
  //  Page *page1 =
  buffer_pool_manager->NewPage(&bucket_page_id);  // 通过指针返回桶id
  //  HASH_TABLE_BUCKET_TYPE *bucket_page = reinterpret_cast<HASH_TABLE_BUCKET_TYPE *>(page1->GetData());

  dir_page->SetBucketPageId(0, bucket_page_id);
  dir_page->SetLocalDepth(0, 0);

  // Unpin
  buffer_pool_manager->UnpinPage(directory_page_id_, true);
  buffer_pool_manager->UnpinPage(bucket_page_id, false);
}

/*****************************************************************************
 * HELPERS
 *****************************************************************************/
/**
 * Hash - simple helper to downcast MurmurHash's 64-bit hash to 32-bit
 * for extendible hashing.
 *
 * @param key the key to hash
 * @return the downcasted 32-bit hash
 */
template <typename KeyType, typename ValueType, typename KeyComparator>
uint32_t HASH_TABLE_TYPE::Hash(KeyType key) {
  return static_cast<uint32_t>(hash_fn_.GetHash(key));
}

template <typename KeyType, typename ValueType, typename KeyComparator>
inline uint32_t HASH_TABLE_TYPE::KeyToDirectoryIndex(KeyType key, HashTableDirectoryPage *dir_page) {
  // 什么是directoryIndex？
  // 什么是GLOBAL_DEPTH_MASK?
  // 为什么这个 DirectoryIndex = Hash(key) & GLOBAL_DEPTH_MASK？
  return Hash(key) & dir_page->GetGlobalDepthMask();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
inline uint32_t HASH_TABLE_TYPE::KeyToPageId(KeyType key, HashTableDirectoryPage *dir_page) {
  return dir_page->GetBucketPageId(KeyToDirectoryIndex(key, dir_page));
}

template <typename KeyType, typename ValueType, typename KeyComparator>
HashTableDirectoryPage *HASH_TABLE_TYPE::FetchDirectoryPage() {
  Page *page = buffer_pool_manager_->FetchPage(directory_page_id_);
  // 注意GetData(), 去除多余不必要的信息。 然后强制转换为HashTableDirectoryPage
  // data_ : The actual data that is stored within a page
  return reinterpret_cast<HashTableDirectoryPage *>(page->GetData());
}

template <typename KeyType, typename ValueType, typename KeyComparator>
HASH_TABLE_BUCKET_TYPE *HASH_TABLE_TYPE::FetchBucketPage(page_id_t bucket_page_id) {
  Page *page = buffer_pool_manager_->FetchPage(bucket_page_id);
  return reinterpret_cast<HASH_TABLE_BUCKET_TYPE *>(page->GetData());
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
template <typename KeyType, typename ValueType, typename KeyComparator>
bool HASH_TABLE_TYPE::GetValue(Transaction *transaction, const KeyType &key, std::vector<ValueType> *result) {
  table_latch_.RLock();
  HashTableDirectoryPage *dir_page = FetchDirectoryPage();
  // 通过key 和 dir_page 找到桶的page_id
  page_id_t bucket_page_id = KeyToPageId(key, dir_page);

  HASH_TABLE_BUCKET_TYPE *bucket_page = FetchBucketPage(bucket_page_id);
  Page *page_lock = reinterpret_cast<Page *>(bucket_page);

  page_lock->RLatch();
  // 拿到值，通过指针返回
  bool flag = bucket_page->GetValue(key, comparator_, result);
  page_lock->RUnlatch();

  // unpin
  buffer_pool_manager_->UnpinPage(directory_page_id_, false);
  buffer_pool_manager_->UnpinPage(bucket_page_id, false);
  table_latch_.RUnlock();
  return flag;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
template <typename KeyType, typename ValueType, typename KeyComparator>
bool HASH_TABLE_TYPE::Insert(Transaction *transaction, const KeyType &key, const ValueType &value) {
  bool flag;
  table_latch_.RLock();
  HashTableDirectoryPage *dir_page = FetchDirectoryPage();

  //    // 获得全局散列深度
  //    uint32_t global_depth_mask =  dir_page->GetGlobalDepthMask();

  page_id_t bucket_page_id = KeyToPageId(key, dir_page);
  HASH_TABLE_BUCKET_TYPE *bucket_page = FetchBucketPage(bucket_page_id);

  // bucket_page 如果满了，则需要调用SplitInsert()为插入的k-v分配空间
  Page *page_lock = reinterpret_cast<Page *>(bucket_page);

  //    if (bucket_page->IsFull()) {
  //        buffer_pool_manager_->UnpinPage(bucket_page_id, false);
  //        buffer_pool_manager_->UnpinPage(directory_page_id_, false);
  //        return SplitInsert(transaction, key, value);
  //    }
  // 如果没满，则插入--->>刷盘
  // 如果有很多在这里锁住，其中一个插入成功后，bucket_page满了需要SplitInsert(),
  // 所以在SplitInsert()前需要判断一下是否需要重新调用Insert();
  page_lock->WLatch();
  flag = bucket_page->Insert(key, value, comparator_);
  page_lock->WUnlatch();
  buffer_pool_manager_->UnpinPage(bucket_page_id, flag);
  buffer_pool_manager_->UnpinPage(directory_page_id_, false);
  table_latch_.RUnlock();

  // 插入失败，调用SplitInsert()
  if (!flag) {
    return SplitInsert(transaction, key, value);
  }
  return flag;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
bool HASH_TABLE_TYPE::SplitInsert(Transaction *transaction, const KeyType &key, const ValueType &value) {
  table_latch_.RUnlock();
  HashTableDirectoryPage *dir_page = FetchDirectoryPage();

  bool flag = false;
  bool is_free = false;

  uint32_t global_depth = dir_page->GetGlobalDepth();
  uint32_t bucket_idx = KeyToDirectoryIndex(key, dir_page);
  uint32_t local_depth = dir_page->GetLocalDepth(bucket_idx);
  page_id_t bucket_page_id = dir_page->GetBucketPageId(bucket_idx);
  HASH_TABLE_BUCKET_TYPE *old_bucket_page = FetchBucketPage(bucket_page_id);

  // 判断是否有重复数据
  uint32_t free_pos = BUCKET_ARRAY_SIZE;
  for (uint32_t i = 0; i < BUCKET_ARRAY_SIZE; ++i) {
    if (free_pos == BUCKET_ARRAY_SIZE) {
      if (!old_bucket_page->IsOccupied(i) && !old_bucket_page->IsReadable(i)) {
        free_pos = i;
      }
    }
    if (old_bucket_page->IsReadable(i) && comparator_(old_bucket_page->KeyAt(i), key) == 0 &&
        old_bucket_page->ValueAt(i) == value) {
      flag = true;
      break;
    }
  }

  // 判断是否重复,若重复调用Insert();
  if (flag) {
    buffer_pool_manager_->UnpinPage(bucket_page_id, is_free);
    buffer_pool_manager_->UnpinPage(directory_page_id_, false);
    table_latch_.RUnlock();
    return false;
  }

  if (free_pos != BUCKET_ARRAY_SIZE) {
    buffer_pool_manager_->UnpinPage(bucket_page_id, is_free);
    buffer_pool_manager_->UnpinPage(directory_page_id_, false);
    table_latch_.RUnlock();
    return Insert(transaction, key, value);
  }

  // 初始化新分裂的bucket_page_id
  page_id_t new_bucket_page_id = INVALID_PAGE_ID;
  uint32_t new_bucket_idx;
  // 申请一个桶页面
  Page *page = buffer_pool_manager_->NewPage(&new_bucket_page_id);
  HASH_TABLE_BUCKET_TYPE *bucket_split_page = reinterpret_cast<HASH_TABLE_BUCKET_TYPE *>(page->GetData());
  // 在overflow的情况下，local_depth一定会+1。
  // now_local_depth：分裂后的局部散列深度
  uint32_t now_local_depth = local_depth + 1;

  //  In case the local depth is less than the global depth, then only Bucket Split takes place.
  //  Then increment only the local depth value by 1. And, assign appropriate pointers.
  if (local_depth < global_depth) {
    // bucket_idx怎么设置？ (0x1 << local_depth) ^ bucket_idx ，保证最低位相同，最高的一位不同
    new_bucket_idx = (0x1 << local_depth) ^ bucket_idx;

    dir_page->SetBucketPageId(new_bucket_idx, new_bucket_page_id);
    // 两个桶的散列深度 +1
    dir_page->IncrLocalDepth(bucket_idx);
    dir_page->IncrLocalDepth(new_bucket_idx);
    uint32_t mask = (0x1 << now_local_depth) - 1;
    // rehash
    for (uint32_t i = 0; i < BUCKET_ARRAY_SIZE; ++i) {
      KeyType key_i = old_bucket_page->KeyAt(i);
      // 比较两个index的最低位
      if ((Hash(key) & mask) == (new_bucket_idx & mask)) {
        bucket_split_page->Insert(key_i, old_bucket_page->ValueAt(i), comparator_);
        old_bucket_page->RemoveAt(i);
        is_free = true;
      }
    }

    // 重新调整local_depth 以及 重新调整directory中指向bucket中的id
    for (uint32_t i = 0; i < DIRECTORY_ARRAY_SIZE; ++i) {
      if (dir_page->GetBucketPageId(i) == bucket_page_id) {
        dir_page->SetLocalDepth(i, local_depth + 1);
        dir_page->SetBucketPageId(i, ((i & mask) == (bucket_idx & mask)) ? bucket_page_id : new_bucket_page_id);
      }
    }

    if (is_free) {
      old_bucket_page->Insert(key, value, comparator_);
      flag = true;
    }
    buffer_pool_manager_->UnpinPage(bucket_page_id, is_free);
    buffer_pool_manager_->UnpinPage(new_bucket_page_id, is_free);
  } else if (local_depth == global_depth) {
    // If the local depth of the overflowing Bucket is equal to the global depth, then Directory Expansion, as well as
    // Bucket Split, needs to be performed. Then increment the global depth and the local depth value by 1. And, assign
    // appropriate pointers. Directory expansion will double the number of directories present in the hash structure.

    // Directory Expansion
    for (uint32_t dir_i = 0; dir_i < dir_page->Size(); ++dir_i) {
      dir_page->SetBucketPageId(dir_i | (01 << global_depth), dir_page->GetBucketPageId(dir_i));
      dir_page->SetLocalDepth(dir_i | (01 << global_depth), dir_page->GetLocalDepth(dir_i));
    }
    dir_page->IncrGlobalDepth();

    // Bucket Split
    // 得到split_bucket_idx 0 -> 10;  00 -> 100
    new_bucket_idx = bucket_idx | (0x1 << global_depth);
    for (uint32_t bucket_i = 0; bucket_i < BUCKET_ARRAY_SIZE; ++bucket_i) {
      KeyType now_key = old_bucket_page->KeyAt(bucket_i);
      if (KeyToDirectoryIndex(now_key, dir_page) == new_bucket_idx) {
        bucket_split_page->Insert(now_key, old_bucket_page->ValueAt(bucket_i), comparator_);
        old_bucket_page->RemoveAt(bucket_i);
        is_free = true;
      }
    }

    // 初始化新桶
    dir_page->SetBucketPageId(new_bucket_idx, new_bucket_page_id);
    dir_page->SetLocalDepth(new_bucket_idx, dir_page->GetGlobalDepth());
    dir_page->SetLocalDepth(bucket_idx, dir_page->GetGlobalDepth());

    // 判断插入到哪一个桶中
    uint32_t insert_idx = KeyToDirectoryIndex(key, dir_page);
    if (insert_idx == bucket_idx && is_free) {
      old_bucket_page->Insert(key, value, comparator_);
      flag = true;
    }
    if (insert_idx == new_bucket_idx) {
      bucket_split_page->Insert(key, value, comparator_);
      flag = true;
    }

    buffer_pool_manager_->UnpinPage(bucket_page_id, is_free);
    buffer_pool_manager_->UnpinPage(new_bucket_page_id, is_free);
  }
  buffer_pool_manager_->UnpinPage(directory_page_id_, true);
  table_latch_.RUnlock();
  if (!flag) {
    return SplitInsert(transaction, key, value);
  }
  return true;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
template <typename KeyType, typename ValueType, typename KeyComparator>
bool HASH_TABLE_TYPE::Remove(Transaction *transaction, const KeyType &key, const ValueType &value) {
  HashTableDirectoryPage *dir_page = FetchDirectoryPage();
  bool flag;
  uint32_t bucket_idx = KeyToDirectoryIndex(key, dir_page);
  page_id_t bucket_page_id = dir_page->GetBucketPageId(bucket_idx);
  HASH_TABLE_BUCKET_TYPE *bucket_page = FetchBucketPage(bucket_page_id);

  flag = bucket_page->Remove(key, value, comparator_);

  // 为什么是false
  buffer_pool_manager_->UnpinPage(directory_page_id_, false);
  buffer_pool_manager_->UnpinPage(bucket_page_id, flag);
  if (flag) {
    Merge(transaction, key, value);
  }
  return flag;
}

/*****************************************************************************
 * MERGE
 *****************************************************************************/
template <typename KeyType, typename ValueType, typename KeyComparator>
void HASH_TABLE_TYPE::Merge(Transaction *transaction, const KeyType &key, const ValueType &value) {
  HashTableDirectoryPage *dir_page = FetchDirectoryPage();
  uint32_t bucket_idx = KeyToDirectoryIndex(key, dir_page);
  uint32_t merge_bucket_idx;
  page_id_t bucket_page_id = dir_page->GetBucketPageId(bucket_idx);
  page_id_t merge_bucket_page_id = INVALID_PAGE_ID;
  HASH_TABLE_BUCKET_TYPE *bucket_page = FetchBucketPage(bucket_page_id);
  //    HASH_TABLE_BUCKET_TYPE *merge_bucket_page = nullptr;
  bool is_merge = false;
  bool is_dirty = false;

  while (true) {
    //      There are three conditions under which we skip the merge:
    //   * 1. The bucket is no longer empty.
    //   * 2. The bucket has local depth 0.
    //   * 3. The bucket's local depth doesn't match its split image's local depth.
    if (bucket_page->IsEmpty() && dir_page->GetLocalDepth(bucket_idx) > 0) {
      // 得到要合并的 bucket_idx,并通过idx找到page_id 以及 page
      merge_bucket_idx = dir_page->GetSplitImageIndex(bucket_idx);
      merge_bucket_page_id = dir_page->GetBucketPageId(merge_bucket_idx);
      //            merge_bucket_page = FetchBucketPage(merge_bucket_page_id);

      if (dir_page->GetLocalDepth(bucket_idx) == dir_page->GetLocalDepth(merge_bucket_idx)) {
        uint32_t size = dir_page->Size();
        // 将原来保存的数据重置
        for (uint32_t bucket_i = 0; bucket_i < size; ++bucket_i) {
          // 开始合并
          is_merge = true;
          is_dirty = true;
          page_id_t cur_page_id = dir_page->GetBucketPageId(bucket_i);

          if (cur_page_id == bucket_page_id || cur_page_id == merge_bucket_page_id) {
            dir_page->SetBucketPageId(bucket_i, merge_bucket_page_id);
            dir_page->IncrLocalDepth(bucket_i);
          }
        }
      }
    }
    buffer_pool_manager_->UnpinPage(bucket_page_id, false);
    if (is_merge) {
      // 合并之后记得删除不需要的页面
      buffer_pool_manager_->DeletePage(bucket_page_id);
      // 判断是否可以收缩directory
      if (dir_page->CanShrink()) {
        dir_page->DecrGlobalDepth();
        // 相当于减少一位
        bucket_idx = bucket_idx & dir_page->GetGlobalDepthMask();
      } else {
        bucket_idx = merge_bucket_idx;
      }
    } else {
      break;
    }
    buffer_pool_manager_->UnpinPage(directory_page_id_, is_dirty);
  }
}

/*****************************************************************************
 * GETGLOBALDEPTH - DO NOT TOUCH
 *****************************************************************************/
template <typename KeyType, typename ValueType, typename KeyComparator>
uint32_t HASH_TABLE_TYPE::GetGlobalDepth() {
  table_latch_.RLock();
  HashTableDirectoryPage *dir_page = FetchDirectoryPage();
  uint32_t global_depth = dir_page->GetGlobalDepth();
  assert(buffer_pool_manager_->UnpinPage(directory_page_id_, false, nullptr));
  table_latch_.RUnlock();
  return global_depth;
}

/*****************************************************************************
 * VERIFY INTEGRITY - DO NOT TOUCH
 *****************************************************************************/
template <typename KeyType, typename ValueType, typename KeyComparator>
void HASH_TABLE_TYPE::VerifyIntegrity() {
  table_latch_.RLock();
  HashTableDirectoryPage *dir_page = FetchDirectoryPage();
  dir_page->VerifyIntegrity();
  assert(buffer_pool_manager_->UnpinPage(directory_page_id_, false, nullptr));
  table_latch_.RUnlock();
}

/*****************************************************************************
 * TEMPLATE DEFINITIONS - DO NOT TOUCH
 *****************************************************************************/
template class ExtendibleHashTable<int, int, IntComparator>;

template class ExtendibleHashTable<GenericKey<4>, RID, GenericComparator<4>>;
template class ExtendibleHashTable<GenericKey<8>, RID, GenericComparator<8>>;
template class ExtendibleHashTable<GenericKey<16>, RID, GenericComparator<16>>;
template class ExtendibleHashTable<GenericKey<32>, RID, GenericComparator<32>>;
template class ExtendibleHashTable<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
