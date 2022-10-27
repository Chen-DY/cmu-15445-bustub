/*
 * @Author: Cdy 925950298@qq.com
 * @Date: 2022-10-13 11:53:03
 * @LastEditors: Cdy 925950298@qq.com
 * @LastEditTime: 2022-10-13 13:18:36
 * @FilePath: /cmu/src/buffer/lru_replacer.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置:
 * https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_replacer.cpp
//
// Identification: src/buffer/lru_replacer.cpp
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_replacer.h"

namespace bustub {

LRUReplacer::LRUReplacer(size_t num_pages) {}

LRUReplacer::~LRUReplacer() = default;

/**
 * 从LRU中得到一个frame_id
 */
bool LRUReplacer::Victim(frame_id_t *frame_id) {
  const std::lock_guard<std::mutex> lock_guard(mux_);
  if (frame_.empty()) {
    return false;
  }
  *frame_id = frame_.front();
  frame_.pop_front();
  frame_map_.erase(*frame_id);
  return true;
}

/**
 * Pin就代表有线程在用这个页，那么我们就要把这个页给钉住，不能让它被换出去
 */
void LRUReplacer::Pin(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock_guard(mux_);
  if (frame_map_.count(frame_id) == 0) {
    return;
  }
  frame_.erase(frame_map_[frame_id]);
  frame_map_.erase(frame_id);
}

void LRUReplacer::Unpin(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock_guard(mux_);
  if (frame_map_.count(frame_id) != 0) {
    return;
  }
  frame_.push_back(frame_id);
  frame_map_[frame_id] = std::prev(frame_.end());
}

size_t LRUReplacer::Size() {
  std::lock_guard<std::mutex> lock_guard(mux_);
  return frame_.size();
}

}  // namespace bustub
