#include "RoomStorage.h"

#include <helpers/TxtDataHelpers.h>

static uint16_t clampLimit(uint16_t limit) {
  if (limit == 0) return 1;
  if (limit > ROOM_STORAGE_MAX_POSTS_PER_CHUNK * 8) {
    return ROOM_STORAGE_MAX_POSTS_PER_CHUNK * 8;
  }
  return limit;
}

LocalRoomStorage::LocalRoomStorage()
    : _write_index(0), _wrapped(false), _result_head(0), _result_tail(0), _client_state_count(0) {
  memset(_posts, 0, sizeof(_posts));
  memset(_result_queue, 0, sizeof(_result_queue));
  memset(_client_states, 0, sizeof(_client_states));
  memset(&_stats, 0, sizeof(_stats));
  _stats.available = true;
  _stats.using_external = false;
}

void LocalRoomStorage::begin() {}

void LocalRoomStorage::loop() {}

bool LocalRoomStorage::isAvailable() const {
  return true;
}

bool LocalRoomStorage::isQueueFull() const {
  return ((uint8_t)(_result_tail + 1) % ROOM_STORAGE_RESULT_QUEUE_DEPTH) == _result_head;
}

bool LocalRoomStorage::enqueueResult(const RoomStorageQueryResult& result) {
  if (isQueueFull()) {
    return false;
  }
  _result_queue[_result_tail] = result;
  _result_tail = (uint8_t)(_result_tail + 1) % ROOM_STORAGE_RESULT_QUEUE_DEPTH;
  return true;
}

bool LocalRoomStorage::pollQueryResult(RoomStorageQueryResult& out_result) {
  if (_result_head == _result_tail) {
    return false;
  }
  out_result = _result_queue[_result_head];
  _result_head = (uint8_t)(_result_head + 1) % ROOM_STORAGE_RESULT_QUEUE_DEPTH;
  return true;
}

bool LocalRoomStorage::hasPostId(uint32_t post_id) const {
  if (post_id == 0) return false;
  uint16_t count = getStoredCount();
  for (uint16_t i = 0; i < count; ++i) {
    const RoomPost& p = _posts[i];
    if (p.post_id == post_id) {
      return true;
    }
  }
  return false;
}

uint16_t LocalRoomStorage::getStoredCount() const {
  return _wrapped ? ROOM_STORAGE_LOCAL_CAPACITY : _write_index;
}

bool LocalRoomStorage::getOldestAndNewestPostId(uint32_t& oldest_id, uint32_t& newest_id) const {
  uint16_t count = getStoredCount();
  if (count == 0) return false;

  oldest_id = 0xFFFFFFFF;
  newest_id = 0;
  for (uint16_t i = 0; i < count; ++i) {
    const RoomPost& p = _posts[i];
    if (p.post_id == 0) continue;
    if (p.post_id < oldest_id) oldest_id = p.post_id;
    if (p.post_id > newest_id) newest_id = p.post_id;
  }
  if (oldest_id == 0xFFFFFFFF) return false;
  return true;
}

bool LocalRoomStorage::appendPost(const RoomPost& post) {
  if (post.post_id == 0) return false;

  if (hasPostId(post.post_id)) {
    return true;
  }

  _posts[_write_index] = post;
  StrHelper::strncpy(_posts[_write_index].text, post.text, sizeof(_posts[_write_index].text));

  _write_index++;
  if (_write_index >= ROOM_STORAGE_LOCAL_CAPACITY) {
    _write_index = 0;
    _wrapped = true;
  }

  _stats.total_posts++;
  if (_wrapped) {
    _stats.dropped_posts++;
  }

  uint32_t oldest_id, newest_id;
  if (getOldestAndNewestPostId(oldest_id, newest_id)) {
    _stats.history_start_post_id = oldest_id;
    _stats.history_end_post_id = newest_id;
  }
  return true;
}

bool LocalRoomStorage::markClientSeen(uint32_t client_id_hash, uint32_t post_id) {
  if (client_id_hash == 0) return false;

  for (uint8_t i = 0; i < _client_state_count; ++i) {
    if (_client_states[i].client_id_hash == client_id_hash) {
      if (post_id > _client_states[i].seen_post_id) {
        _client_states[i].seen_post_id = post_id;
      }
      _client_states[i].last_seen_time = millis();
      return true;
    }
  }

  if (_client_state_count >= (sizeof(_client_states) / sizeof(_client_states[0]))) {
    return false;
  }

  RoomClientState& state = _client_states[_client_state_count++];
  state.client_id_hash = client_id_hash;
  state.seen_post_id = post_id;
  state.last_seen_time = millis();
  return true;
}

bool LocalRoomStorage::getClientState(uint32_t client_id_hash, RoomClientState& out_state) {
  for (uint8_t i = 0; i < _client_state_count; ++i) {
    if (_client_states[i].client_id_hash == client_id_hash) {
      out_state = _client_states[i];
      return true;
    }
  }
  return false;
}

RoomStorageStats LocalRoomStorage::getStats() const {
  return _stats;
}

bool LocalRoomStorage::getLast(uint16_t request_seq, uint8_t client_idx, uint16_t limit) {
  uint16_t stored = getStoredCount();
  limit = clampLimit(limit);
  if (stored == 0) {
    RoomStorageQueryResult done = {request_seq, client_idx, ROOM_STORAGE_OK, true, 0, {}};
    return enqueueResult(done);
  }

  RoomPost matched[ROOM_STORAGE_LOCAL_CAPACITY];
  uint16_t matched_count = 0;
  for (uint16_t i = 0; i < ROOM_STORAGE_LOCAL_CAPACITY; ++i) {
    if (_posts[i].post_id != 0) {
      matched[matched_count++] = _posts[i];
    }
  }
  if (matched_count == 0) {
    RoomStorageQueryResult done = {request_seq, client_idx, ROOM_STORAGE_OK, true, 0, {}};
    return enqueueResult(done);
  }

  for (uint16_t i = 0; i + 1 < matched_count; ++i) {
    for (uint16_t j = i + 1; j < matched_count; ++j) {
      if (matched[j].post_id < matched[i].post_id) {
        RoomPost tmp = matched[i];
        matched[i] = matched[j];
        matched[j] = tmp;
      }
    }
  }

  uint16_t start = (matched_count > limit) ? (matched_count - limit) : 0;
  uint16_t idx = start;
  while (idx < matched_count) {
    RoomStorageQueryResult result = {request_seq, client_idx, ROOM_STORAGE_OK, false, 0, {}};
    while (idx < matched_count && result.post_count < ROOM_STORAGE_MAX_POSTS_PER_CHUNK) {
      result.posts[result.post_count++] = matched[idx++];
    }
    if (idx >= matched_count) {
      result.done = true;
    }
    if (!enqueueResult(result)) {
      return false;
    }
  }
  return true;
}

bool LocalRoomStorage::getAfterId(uint16_t request_seq, uint8_t client_idx, uint32_t post_id, uint16_t limit) {
  limit = clampLimit(limit);
  uint32_t oldest_id, newest_id;
  if (getOldestAndNewestPostId(oldest_id, newest_id) && post_id != 0 && post_id < oldest_id) {
    RoomStorageQueryResult gap = {request_seq, client_idx, ROOM_STORAGE_HISTORY_GAP, true, 0, {}};
    return enqueueResult(gap);
  }

  RoomPost matched[ROOM_STORAGE_LOCAL_CAPACITY];
  uint16_t matched_count = 0;
  for (uint16_t i = 0; i < ROOM_STORAGE_LOCAL_CAPACITY; ++i) {
    if (_posts[i].post_id > post_id) {
      matched[matched_count++] = _posts[i];
    }
  }

  for (uint16_t i = 0; i + 1 < matched_count; ++i) {
    for (uint16_t j = i + 1; j < matched_count; ++j) {
      if (matched[j].post_id < matched[i].post_id) {
        RoomPost tmp = matched[i];
        matched[i] = matched[j];
        matched[j] = tmp;
      }
    }
  }

  if (matched_count > limit) {
    matched_count = limit;
  }

  if (matched_count == 0) {
    RoomStorageQueryResult done = {request_seq, client_idx, ROOM_STORAGE_OK, true, 0, {}};
    return enqueueResult(done);
  }

  uint16_t idx = 0;
  while (idx < matched_count) {
    RoomStorageQueryResult result = {request_seq, client_idx, ROOM_STORAGE_OK, false, 0, {}};
    while (idx < matched_count && result.post_count < ROOM_STORAGE_MAX_POSTS_PER_CHUNK) {
      result.posts[result.post_count++] = matched[idx++];
    }
    if (idx >= matched_count) {
      result.done = true;
    }
    if (!enqueueResult(result)) {
      return false;
    }
  }
  return true;
}

bool LocalRoomStorage::getSince(uint16_t request_seq, uint8_t client_idx, uint32_t timestamp, uint16_t limit) {
  limit = clampLimit(limit);

  RoomPost matched[ROOM_STORAGE_LOCAL_CAPACITY];
  uint16_t matched_count = 0;
  for (uint16_t i = 0; i < ROOM_STORAGE_LOCAL_CAPACITY; ++i) {
    if (_posts[i].post_id != 0 && _posts[i].post_timestamp > timestamp) {
      matched[matched_count++] = _posts[i];
    }
  }

  for (uint16_t i = 0; i + 1 < matched_count; ++i) {
    for (uint16_t j = i + 1; j < matched_count; ++j) {
      if (matched[j].post_id < matched[i].post_id) {
        RoomPost tmp = matched[i];
        matched[i] = matched[j];
        matched[j] = tmp;
      }
    }
  }

  if (matched_count > limit) {
    matched_count = limit;
  }

  if (matched_count == 0) {
    RoomStorageQueryResult done = {request_seq, client_idx, ROOM_STORAGE_OK, true, 0, {}};
    return enqueueResult(done);
  }

  uint16_t idx = 0;
  while (idx < matched_count) {
    RoomStorageQueryResult result = {request_seq, client_idx, ROOM_STORAGE_OK, false, 0, {}};
    while (idx < matched_count && result.post_count < ROOM_STORAGE_MAX_POSTS_PER_CHUNK) {
      result.posts[result.post_count++] = matched[idx++];
    }
    if (idx >= matched_count) {
      result.done = true;
    }
    if (!enqueueResult(result)) {
      return false;
    }
  }
  return true;
}
