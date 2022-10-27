//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_instance.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager_instance.h"

#include "common/macros.h"

namespace bustub {

BufferPoolManagerInstance::BufferPoolManagerInstance(size_t pool_size, DiskManager *disk_manager,
                                                     LogManager *log_manager)
    : BufferPoolManagerInstance(pool_size, 1, 0, disk_manager, log_manager) {}

BufferPoolManagerInstance::BufferPoolManagerInstance(size_t pool_size, uint32_t num_instances, uint32_t instance_index,
                                                     DiskManager *disk_manager, LogManager *log_manager)
    : pool_size_(pool_size),
      num_instances_(num_instances),
      instance_index_(instance_index),
      next_page_id_(instance_index),
      disk_manager_(disk_manager),
      log_manager_(log_manager) {
  BUSTUB_ASSERT(num_instances > 0, "If BPI is not part of a pool, then the pool size should just be 1");
  BUSTUB_ASSERT(instance_index < num_instances,
                "BPI index cannot be greater than the number of BPIs in the "
                "pool. In non-parallel case, index should just be 1.");
  // We allocate a consecutive memory space for the buffer pool.
  pages_ = new Page[pool_size_];
  replacer_ = new LRUReplacer(pool_size);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }

  std::ifstream file("/autograder/bustub/test/buffer/grading_buffer_pool_manager_instance_test.cpp");
  while (file) {
    std::string line;
    std::getline(file, line);
    std::cout << line << std::endl;
  }
}

BufferPoolManagerInstance::~BufferPoolManagerInstance() {
  delete[] pages_;
  delete replacer_;
}

bool BufferPoolManagerInstance::FlushPgImp(page_id_t page_id) {
  // Make sure you call DiskManager::WritePage!
  std::lock_guard<std::mutex> lock_guard(latch_);

  auto it = page_table_.find(page_id);

  // 如果在page_table中找不到 或者page_id是INVALID_PAGE_ID 则返回false
  if (it == page_table_.end() || page_id == INVALID_PAGE_ID) {
    return false;
  }

  // 拿到page的frame_id
  frame_id_t flush_id = it->second;
  // 向磁盘写入数据
  disk_manager_->WritePage(page_id, pages_[flush_id].data_);

  return true;
}

void BufferPoolManagerInstance::FlushAllPgsImp() {
  // You can do it!
  std::lock_guard<std::mutex> lock_guard(latch_);
  for (auto &it : page_table_) {
    if (it.first == INVALID_PAGE_ID) {
      continue;
    }
    frame_id_t flush_id = it.second;
    disk_manager_->WritePage(it.first, pages_[flush_id].data_);
  }
}

Page *BufferPoolManagerInstance::NewPgImp(page_id_t *page_id) {
  const std::lock_guard<std::mutex> lock_guard(latch_);

  // 0.   Make sure you call AllocatePage!
  // 罗老师说要在后面分配
  // 1.   If all the pages in the buffer pool are pinned, return nullptr.
  if (free_list_.empty() && replacer_->Size() == 0) {
    return nullptr;
  }
  // 2.   Pick a victim page P from either the free list or the replacer. Always
  // pick from the free list first.
  frame_id_t p;
  // 如果free_list_不为空，则从free_list_中获得一个frame_id，否则就调用Victim()从LRU中获得一个frame_id
  if (!free_list_.empty()) {
    p = free_list_.front();
    free_list_.pop_front();
  } else {
    replacer_->Victim(&p);
  }
  // 3.   Update P's metadata, zero out memory and add P to the page table.
  Page *victim_page = &pages_[p];
  if (victim_page->IsDirty()) {
    // 有问题
    disk_manager_->WritePage(victim_page->GetPageId(), victim_page->data_);
  }
  // 解除映射
  page_table_.erase(victim_page->GetPageId());

  *page_id = AllocatePage();
  victim_page->page_id_ = *page_id;
  // victim_page->page_id_ = AllocatePage();
  // *page_id = victim_page->page_id_;
  victim_page->ResetMemory();
  victim_page->is_dirty_ = false;
  victim_page->pin_count_ = 1;

  // 重新建立映射
  page_table_[victim_page->GetPageId()] = p;
  // 4.   Set the page ID output parameter. Return a pointer to P.
  return victim_page;
}

Page *BufferPoolManagerInstance::FetchPgImp(page_id_t page_id) {
  std::lock_guard<std::mutex> lock_guard(latch_);
  Page *fetch_page = nullptr;
  // 1.     Search the page table for the requested page (P).
  // 1.1    If P exists, pin it and return it immediately.
  if (page_table_.count(page_id) != 0) {
    frame_id_t frame_id = page_table_[page_id];
    fetch_page = &pages_[frame_id];
    fetch_page->pin_count_ += 1;
    replacer_->Pin(page_table_[page_id]);
    return fetch_page;
  }
  // 1.2    If P does not exist, find a replacement page (R) from either the
  // free list or the replacer.
  //        Note that pages are always found from the free list first.
  if (free_list_.empty() && replacer_->Size() == 0) {
    return nullptr;
  }
  frame_id_t frame_id = -1;
  // 如果free_list_不为空，则从free_list_中获得一个frame_id，否则就调用Victim()从LRU中获得一个frame_id
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else {
    replacer_->Victim(&frame_id);
  }
  fetch_page = &pages_[frame_id];

  // 2.     If R is dirty, write it back to the disk.
  if (fetch_page->is_dirty_) {
    disk_manager_->WritePage(fetch_page->GetPageId(), fetch_page->data_);
  }
  // 3.     Delete R from the page table and insert P.
  page_table_.erase(fetch_page->GetPageId());
  page_table_[page_id] = frame_id;
  // 4.     Update P's metadata, read in the page content from disk, and then
  // return a pointer to P.
  disk_manager_->ReadPage(page_id, fetch_page->data_);
  fetch_page->page_id_ = page_id;
  fetch_page->pin_count_ = 1;
  fetch_page->is_dirty_ = false;
  return fetch_page;
}

bool BufferPoolManagerInstance::DeletePgImp(page_id_t page_id) {
  std::lock_guard<std::mutex> lock_guard(latch_);
  // 0.   Make sure you call DeallocatePage! 没有DeallocatePage?
  // 1.   Search the page table for the requested page (P).
  auto iter = page_table_.find(page_id);

  // 1.   If P does not exist, return true.
  if (page_table_.count(page_id) == 0) {
    return true;
  }
  // 2.   If P exists, but has a non-zero pin-count, return false. Someone is
  // using the page.
  Page *p = &pages_[iter->second];
  // pages_[page_table_[page_id]].pin_count_
  if (p->pin_count_ != 0) {
    return false;
  }
  // 3.   Otherwise, P can be deleted. Remove P from the page table, reset its
  // metadata and return it to the free list.
  if (p->IsDirty()) {
    disk_manager_->WritePage(p->GetPageId(), p->GetData());
  }

  p->ResetMemory();
  p->is_dirty_ = false;
  p->pin_count_ = 0;
  p->page_id_ = INVALID_PAGE_ID;

  free_list_.push_back(page_table_[page_id]);
  // 注意这里要确保只在replacer,free_list的其中一个里面出现
  replacer_->Pin(iter->second);
  page_table_.erase(page_id);
  return true;
}

/**
 * 这个函数就是如果这个进程已经完成了对这个页的操作。我要unpin操作

    如果这个页的pin_couter>0直接--

    如果这个页的pin _couter==0需要给它加到Lru_replacer中。因为没有人引用它。所以它可以成为被替换的候选人
*/
bool BufferPoolManagerInstance::UnpinPgImp(page_id_t page_id, bool is_dirty) {
  std::lock_guard<std::mutex> lock_guard(latch_);
  auto it = page_table_.find(page_id);

  if (it == page_table_.end()) {
    return false;
  }
  Page *p = &pages_[it->second];
  if (p->pin_count_ == 0) {
    return false;
  }

  p->is_dirty_ = is_dirty || p->IsDirty();
  p->pin_count_ -= 1;

  if (p->pin_count_ == 0) {
    replacer_->Unpin(it->second);
  }

  return true;
}

page_id_t BufferPoolManagerInstance::AllocatePage() {
  const page_id_t next_page_id = next_page_id_;
  next_page_id_ += num_instances_;
  ValidatePageId(next_page_id);
  return next_page_id;
}

void BufferPoolManagerInstance::ValidatePageId(const page_id_t page_id) const {
  assert(page_id % num_instances_ == instance_index_);  // allocated pages mod back to this BPI
}

}  // namespace bustub
