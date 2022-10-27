//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// parallel_buffer_pool_manager.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/parallel_buffer_pool_manager.h"

namespace bustub {

ParallelBufferPoolManager::ParallelBufferPoolManager(size_t num_instances, size_t pool_size, DiskManager *disk_manager,
                                                     LogManager *log_manager)
    : num_instances_(num_instances), pool_size_(pool_size) {
  // Allocate and create individual BufferPoolManagerInstances
  for (size_t i = 0; i < num_instances_; i++) {
    BufferPoolManager *temp = new BufferPoolManagerInstance(pool_size, num_instances, i, disk_manager, log_manager);
    buffer_pool_instance_.emplace_back(temp);
  }
}

// Update constructor to destruct all BufferPoolManagerInstances and deallocate
// any associated memory
ParallelBufferPoolManager::~ParallelBufferPoolManager() {
  for (size_t i = 0; i < num_instances_; i++) {
    // buffer_pool_instance_.pop_back();
    delete buffer_pool_instance_[i];
  }
}

size_t ParallelBufferPoolManager::GetPoolSize() {
  // Get size of all BufferPoolManagerInstances
  return num_instances_ * pool_size_;
}

BufferPoolManager *ParallelBufferPoolManager::GetBufferPoolManager(page_id_t page_id) {
  // Get BufferPoolManager responsible for handling given page id. You can use
  // this method in your other methods.
  return buffer_pool_instance_[page_id % num_instances_];
}

Page *ParallelBufferPoolManager::FetchPgImp(page_id_t page_id) {
  // Fetch page for page_id from responsible BufferPoolManagerInstance
  auto *it = GetBufferPoolManager(page_id);
  return it->FetchPage(page_id);
}

bool ParallelBufferPoolManager::UnpinPgImp(page_id_t page_id, bool is_dirty) {
  // Unpin page_id from responsible BufferPoolManagerInstance
  return GetBufferPoolManager(page_id)->UnpinPage(page_id, is_dirty);
}

bool ParallelBufferPoolManager::FlushPgImp(page_id_t page_id) {
  // Flush page_id from responsible BufferPoolManagerInstance
  return GetBufferPoolManager(page_id)->FlushPage(page_id);
}

Page *ParallelBufferPoolManager::NewPgImp(page_id_t *page_id) {
  // create new page. We will request page allocation in a round robin manner
  // from the underlying BufferPoolManagerInstances
  // 1.   From a starting index of the BPMIs, call NewPageImpl until either 1)
  // success and return 2) looped around to starting index and return nullptr
  // 2.   Bump the starting index (mod number of instances) to start search at a
  // different BPMI each time this function is called
  Page *ret = nullptr;
  for (size_t i = 0; i < num_instances_; i++) {
    size_t idx = (start_index_ + i) % num_instances_;
    if ((ret = buffer_pool_instance_[idx]->NewPage(page_id)) != nullptr) {
      start_index_ = (*page_id + 1) % num_instances_;
      return ret;
    }
  }
  start_index_++;
  return nullptr;
}

bool ParallelBufferPoolManager::DeletePgImp(page_id_t page_id) {
  // Delete page_id from responsible BufferPoolManagerInstance
  return GetBufferPoolManager(page_id)->DeletePage(page_id);
}

void ParallelBufferPoolManager::FlushAllPgsImp() {
  // flush all pages from all BufferPoolManagerInstances
  for (size_t i = 0; i < num_instances_; i++) {
    buffer_pool_instance_[i]->FlushAllPages();
  }
}

}  // namespace bustub
