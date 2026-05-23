#pragma once

#include <Arduino.h>
#include <Mesh.h>

#ifndef ROOM_POST_TEXT_MAX
  #define ROOM_POST_TEXT_MAX (160 - 9)
#endif

#ifndef ROOM_STORAGE_LOCAL_CAPACITY
  #define ROOM_STORAGE_LOCAL_CAPACITY 96
#endif

#ifndef ROOM_STORAGE_MAX_POSTS_PER_CHUNK
  #define ROOM_STORAGE_MAX_POSTS_PER_CHUNK 5
#endif

#ifndef ROOM_STORAGE_RESULT_QUEUE_DEPTH
  #define ROOM_STORAGE_RESULT_QUEUE_DEPTH 12
#endif

struct RoomPost {
  uint32_t post_id;
  mesh::Identity author;
  uint32_t post_timestamp;
  char text[ROOM_POST_TEXT_MAX + 1];
};

struct RoomClientState {
  uint32_t client_id_hash;
  uint32_t seen_post_id;
  uint32_t last_seen_time;
};

struct RoomStorageStats {
  bool available;
  bool using_external;
  uint32_t total_posts;
  uint32_t dropped_posts;
  uint32_t crc_errors;
  uint32_t history_start_post_id;
  uint32_t history_end_post_id;
};

enum RoomStorageResultCode : uint8_t {
  ROOM_STORAGE_OK = 0,
  ROOM_STORAGE_ERR = 1,
  ROOM_STORAGE_BUSY = 2,
  ROOM_STORAGE_CRC_ERROR = 3,
  ROOM_STORAGE_HISTORY_GAP = 4,
  ROOM_STORAGE_UNAVAILABLE = 5,
};

struct RoomStorageQueryResult {
  uint16_t request_seq;
  uint8_t client_idx;
  RoomStorageResultCode code;
  bool done;
  uint8_t post_count;
  RoomPost posts[ROOM_STORAGE_MAX_POSTS_PER_CHUNK];
};

class RoomStorage {
public:
  virtual ~RoomStorage() {}

  virtual void begin() = 0;
  virtual void loop() = 0;
  virtual bool isAvailable() const = 0;

  virtual bool appendPost(const RoomPost& post) = 0;
  virtual bool getLast(uint16_t request_seq, uint8_t client_idx, uint16_t limit) = 0;
  virtual bool getAfterId(uint16_t request_seq, uint8_t client_idx, uint32_t post_id, uint16_t limit) = 0;
  virtual bool getSince(uint16_t request_seq, uint8_t client_idx, uint32_t timestamp, uint16_t limit) = 0;

  virtual bool markClientSeen(uint32_t client_id_hash, uint32_t post_id) = 0;
  virtual bool getClientState(uint32_t client_id_hash, RoomClientState& out_state) = 0;

  virtual RoomStorageStats getStats() const = 0;
  virtual bool pollQueryResult(RoomStorageQueryResult& out_result) = 0;
};

class LocalRoomStorage : public RoomStorage {
public:
  LocalRoomStorage();

  void begin() override;
  void loop() override;
  bool isAvailable() const override;

  bool appendPost(const RoomPost& post) override;
  bool getLast(uint16_t request_seq, uint8_t client_idx, uint16_t limit) override;
  bool getAfterId(uint16_t request_seq, uint8_t client_idx, uint32_t post_id, uint16_t limit) override;
  bool getSince(uint16_t request_seq, uint8_t client_idx, uint32_t timestamp, uint16_t limit) override;

  bool markClientSeen(uint32_t client_id_hash, uint32_t post_id) override;
  bool getClientState(uint32_t client_id_hash, RoomClientState& out_state) override;

  RoomStorageStats getStats() const override;
  bool pollQueryResult(RoomStorageQueryResult& out_result) override;

private:
  RoomPost _posts[ROOM_STORAGE_LOCAL_CAPACITY];
  uint16_t _write_index;
  bool _wrapped;

  RoomStorageQueryResult _result_queue[ROOM_STORAGE_RESULT_QUEUE_DEPTH];
  uint8_t _result_head;
  uint8_t _result_tail;

  RoomClientState _client_states[16];
  uint8_t _client_state_count;

  RoomStorageStats _stats;

  bool enqueueResult(const RoomStorageQueryResult& result);
  bool isQueueFull() const;
  bool hasPostId(uint32_t post_id) const;
  uint16_t getStoredCount() const;
  bool getOldestAndNewestPostId(uint32_t& oldest_id, uint32_t& newest_id) const;
};
