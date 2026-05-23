#pragma once

#include "RoomStorage.h"

#include <Stream.h>

#ifndef USE_EXTERNAL_MCU_ROOM_STORAGE
  #define USE_EXTERNAL_MCU_ROOM_STORAGE 0
#endif

#if USE_EXTERNAL_MCU_ROOM_STORAGE

enum ExternalStorageCmdType : uint8_t {
  EXT_CMD_PING = 0x01,
  EXT_CMD_APPEND_POST = 0x02,
  EXT_CMD_GET_LAST = 0x03,
  EXT_CMD_GET_AFTER_ID = 0x04,
  EXT_CMD_GET_SINCE_TIME = 0x05,
  EXT_CMD_MARK_CLIENT_SEEN = 0x06,
  EXT_CMD_GET_CLIENT_STATE = 0x07,
  EXT_CMD_GET_STATS = 0x08,

  EXT_RESP_OK = 0x10,
  EXT_RESP_ERR = 0x11,
  EXT_RESP_BUSY = 0x12,
  EXT_RESP_POST_CHUNK = 0x13,
  EXT_RESP_DONE = 0x14,
  EXT_RESP_CRC_ERROR = 0x15,
  EXT_RESP_ERR_HISTORY_GAP = 0x16,
};

class ExternalMcuRoomStorage : public RoomStorage {
public:
  explicit ExternalMcuRoomStorage(Stream& stream);

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
  struct PendingRequest {
    bool active;
    uint16_t seq;
    uint8_t client_idx;
    uint32_t started_at;
  };

  Stream* _stream;
  bool _available;
  uint32_t _last_ping_at;
  uint32_t _last_rx_at;

  uint16_t _next_seq;
  PendingRequest _pending[8];

  RoomStorageQueryResult _result_queue[ROOM_STORAGE_RESULT_QUEUE_DEPTH];
  uint8_t _result_head;
  uint8_t _result_tail;

  RoomStorageStats _stats;

  uint8_t _frame_buf[512];
  uint16_t _frame_pos;
  uint16_t _expected_payload;

  uint16_t nextSeq();
  bool enqueueResult(const RoomStorageQueryResult& result);
  bool sendFrame(uint8_t type, uint16_t seq, const uint8_t* payload, uint16_t payload_len);
  bool sendSimple32x2(uint8_t type, uint16_t seq, uint32_t a, uint32_t b);
  bool queuePending(uint16_t seq, uint8_t client_idx);
  PendingRequest* findPending(uint16_t seq);
  void clearPending(uint16_t seq);

  void pollFrames();
  void processFrame(const uint8_t* frame, uint16_t len);

  static uint32_t crc32(const uint8_t* data, size_t len);
};

#endif
