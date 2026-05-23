#include "ExternalMcuRoomStorage.h"

#if USE_EXTERNAL_MCU_ROOM_STORAGE

#include <HardwareSerial.h>
#include <helpers/StrHelper.h>

#ifndef EXTERNAL_ROOM_STORAGE_UART_TIMEOUT_MS
  #define EXTERNAL_ROOM_STORAGE_UART_TIMEOUT_MS 350
#endif

#ifndef EXTERNAL_ROOM_STORAGE_PING_INTERVAL_MS
  #define EXTERNAL_ROOM_STORAGE_PING_INTERVAL_MS 2500
#endif

#ifndef EXTERNAL_ROOM_STORAGE_MAGIC
  #define EXTERNAL_ROOM_STORAGE_MAGIC 0xA55A
#endif

#ifndef EXTERNAL_ROOM_STORAGE_VERSION
  #define EXTERNAL_ROOM_STORAGE_VERSION 1
#endif

ExternalMcuRoomStorage::ExternalMcuRoomStorage(Stream& stream)
    : _stream(&stream), _available(false), _last_ping_at(0), _last_rx_at(0), _next_seq(1),
      _result_head(0), _result_tail(0), _frame_pos(0), _expected_payload(0) {
  memset(_pending, 0, sizeof(_pending));
  memset(_result_queue, 0, sizeof(_result_queue));
  memset(&_stats, 0, sizeof(_stats));
  _stats.available = false;
  _stats.using_external = true;
}

void ExternalMcuRoomStorage::begin() {
  _last_ping_at = 0;
  _last_rx_at = millis();
}

bool ExternalMcuRoomStorage::isAvailable() const {
  return _available;
}

uint16_t ExternalMcuRoomStorage::nextSeq() {
  _next_seq++;
  if (_next_seq == 0) _next_seq = 1;
  return _next_seq;
}

bool ExternalMcuRoomStorage::enqueueResult(const RoomStorageQueryResult& result) {
  uint8_t next = (uint8_t)(_result_tail + 1) % ROOM_STORAGE_RESULT_QUEUE_DEPTH;
  if (next == _result_head) return false;
  _result_queue[_result_tail] = result;
  _result_tail = next;
  return true;
}

bool ExternalMcuRoomStorage::pollQueryResult(RoomStorageQueryResult& out_result) {
  if (_result_head == _result_tail) return false;
  out_result = _result_queue[_result_head];
  _result_head = (uint8_t)(_result_head + 1) % ROOM_STORAGE_RESULT_QUEUE_DEPTH;
  return true;
}

uint32_t ExternalMcuRoomStorage::crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      uint32_t mask = -(crc & 1);
      crc = (crc >> 1) ^ (0xEDB88320 & mask);
    }
  }
  return ~crc;
}

bool ExternalMcuRoomStorage::sendFrame(uint8_t type, uint16_t seq, const uint8_t* payload, uint16_t payload_len) {
  uint8_t frame[512];
  if (payload_len + 12 > sizeof(frame)) return false;

  frame[0] = (uint8_t)((EXTERNAL_ROOM_STORAGE_MAGIC >> 8) & 0xFF);
  frame[1] = (uint8_t)(EXTERNAL_ROOM_STORAGE_MAGIC & 0xFF);
  frame[2] = EXTERNAL_ROOM_STORAGE_VERSION;
  frame[3] = type;
  frame[4] = (uint8_t)((seq >> 8) & 0xFF);
  frame[5] = (uint8_t)(seq & 0xFF);
  frame[6] = (uint8_t)((payload_len >> 8) & 0xFF);
  frame[7] = (uint8_t)(payload_len & 0xFF);
  if (payload_len > 0) {
    memcpy(&frame[8], payload, payload_len);
  }

  uint32_t crc = crc32(frame, (size_t)8 + payload_len);
  uint16_t o = (uint16_t)(8 + payload_len);
  frame[o + 0] = (uint8_t)((crc >> 24) & 0xFF);
  frame[o + 1] = (uint8_t)((crc >> 16) & 0xFF);
  frame[o + 2] = (uint8_t)((crc >> 8) & 0xFF);
  frame[o + 3] = (uint8_t)(crc & 0xFF);

  _stream->write(frame, payload_len + 12);
  return true;
}

bool ExternalMcuRoomStorage::sendSimple32x2(uint8_t type, uint16_t seq, uint32_t a, uint32_t b) {
  uint8_t payload[8];
  memcpy(&payload[0], &a, 4);
  memcpy(&payload[4], &b, 4);
  return sendFrame(type, seq, payload, sizeof(payload));
}

bool ExternalMcuRoomStorage::queuePending(uint16_t seq, uint8_t client_idx) {
  for (size_t i = 0; i < sizeof(_pending) / sizeof(_pending[0]); ++i) {
    if (!_pending[i].active) {
      _pending[i].active = true;
      _pending[i].seq = seq;
      _pending[i].client_idx = client_idx;
      _pending[i].started_at = millis();
      return true;
    }
  }
  return false;
}

ExternalMcuRoomStorage::PendingRequest* ExternalMcuRoomStorage::findPending(uint16_t seq) {
  for (size_t i = 0; i < sizeof(_pending) / sizeof(_pending[0]); ++i) {
    if (_pending[i].active && _pending[i].seq == seq) {
      return &_pending[i];
    }
  }
  return NULL;
}

void ExternalMcuRoomStorage::clearPending(uint16_t seq) {
  PendingRequest* p = findPending(seq);
  if (p) {
    memset(p, 0, sizeof(*p));
  }
}

bool ExternalMcuRoomStorage::appendPost(const RoomPost& post) {
  uint8_t payload[4 + 4 + 32 + 1 + ROOM_POST_TEXT_MAX + 1];
  uint16_t len = 0;

  memcpy(&payload[len], &post.post_id, 4);
  len += 4;
  memcpy(&payload[len], &post.post_timestamp, 4);
  len += 4;
  memcpy(&payload[len], post.author.pub_key, PUB_KEY_SIZE);
  len += PUB_KEY_SIZE;

  uint8_t text_len = (uint8_t)min((size_t)ROOM_POST_TEXT_MAX, strlen(post.text));
  payload[len++] = text_len;
  memcpy(&payload[len], post.text, text_len);
  len += text_len;

  uint16_t seq = nextSeq();
  return sendFrame(EXT_CMD_APPEND_POST, seq, payload, len);
}

bool ExternalMcuRoomStorage::getLast(uint16_t request_seq, uint8_t client_idx, uint16_t limit) {
  uint8_t payload[2];
  payload[0] = (uint8_t)(limit & 0xFF);
  payload[1] = (uint8_t)((limit >> 8) & 0xFF);
  if (!queuePending(request_seq, client_idx)) return false;
  return sendFrame(EXT_CMD_GET_LAST, request_seq, payload, sizeof(payload));
}

bool ExternalMcuRoomStorage::getAfterId(uint16_t request_seq, uint8_t client_idx, uint32_t post_id, uint16_t limit) {
  uint8_t payload[6];
  memcpy(&payload[0], &post_id, 4);
  payload[4] = (uint8_t)(limit & 0xFF);
  payload[5] = (uint8_t)((limit >> 8) & 0xFF);
  if (!queuePending(request_seq, client_idx)) return false;
  return sendFrame(EXT_CMD_GET_AFTER_ID, request_seq, payload, sizeof(payload));
}

bool ExternalMcuRoomStorage::getSince(uint16_t request_seq, uint8_t client_idx, uint32_t timestamp, uint16_t limit) {
  uint8_t payload[6];
  memcpy(&payload[0], &timestamp, 4);
  payload[4] = (uint8_t)(limit & 0xFF);
  payload[5] = (uint8_t)((limit >> 8) & 0xFF);
  if (!queuePending(request_seq, client_idx)) return false;
  return sendFrame(EXT_CMD_GET_SINCE_TIME, request_seq, payload, sizeof(payload));
}

bool ExternalMcuRoomStorage::markClientSeen(uint32_t client_id_hash, uint32_t post_id) {
  uint16_t seq = nextSeq();
  return sendSimple32x2(EXT_CMD_MARK_CLIENT_SEEN, seq, client_id_hash, post_id);
}

bool ExternalMcuRoomStorage::getClientState(uint32_t client_id_hash, RoomClientState& out_state) {
  (void)out_state;
  uint16_t seq = nextSeq();
  return sendSimple32x2(EXT_CMD_GET_CLIENT_STATE, seq, client_id_hash, 0);
}

RoomStorageStats ExternalMcuRoomStorage::getStats() const {
  return _stats;
}

void ExternalMcuRoomStorage::pollFrames() {
  while (_stream->available()) {
    int v = _stream->read();
    if (v < 0) break;
    uint8_t b = (uint8_t)v;

    if (_frame_pos < 2) {
      uint8_t magic_hi = (uint8_t)((EXTERNAL_ROOM_STORAGE_MAGIC >> 8) & 0xFF);
      uint8_t magic_lo = (uint8_t)(EXTERNAL_ROOM_STORAGE_MAGIC & 0xFF);
      if ((_frame_pos == 0 && b == magic_hi) || (_frame_pos == 1 && b == magic_lo)) {
        _frame_buf[_frame_pos++] = b;
      } else {
        _frame_pos = 0;
      }
      continue;
    }

    if (_frame_pos < sizeof(_frame_buf)) {
      _frame_buf[_frame_pos++] = b;
    } else {
      _frame_pos = 0;
      continue;
    }

    if (_frame_pos == 8) {
      _expected_payload = ((uint16_t)_frame_buf[6] << 8) | _frame_buf[7];
      if (_expected_payload > sizeof(_frame_buf) - 12) {
        _frame_pos = 0;
      }
    }

    if (_frame_pos >= 8 && _frame_pos == (uint16_t)(12 + _expected_payload)) {
      processFrame(_frame_buf, _frame_pos);
      _frame_pos = 0;
      _last_rx_at = millis();
      _available = true;
    }
  }
}

void ExternalMcuRoomStorage::processFrame(const uint8_t* frame, uint16_t len) {
  if (len < 12) return;

  uint16_t payload_len = ((uint16_t)frame[6] << 8) | frame[7];
  if (payload_len + 12 != len) return;

  uint32_t wire_crc = ((uint32_t)frame[len - 4] << 24) |
                      ((uint32_t)frame[len - 3] << 16) |
                      ((uint32_t)frame[len - 2] << 8) |
                      ((uint32_t)frame[len - 1]);
  uint32_t calc_crc = crc32(frame, len - 4);
  uint16_t seq = ((uint16_t)frame[4] << 8) | frame[5];

  if (wire_crc != calc_crc) {
    _stats.crc_errors++;
    PendingRequest* p = findPending(seq);
    if (p) {
      RoomStorageQueryResult r = {seq, p->client_idx, ROOM_STORAGE_CRC_ERROR, true, 0, {}};
      enqueueResult(r);
      clearPending(seq);
    }
    return;
  }

  const uint8_t type = frame[3];
  const uint8_t* payload = &frame[8];
  PendingRequest* pending = findPending(seq);

  if (type == EXT_RESP_POST_CHUNK && pending) {
    RoomStorageQueryResult r = {seq, pending->client_idx, ROOM_STORAGE_OK, false, 0, {}};
    if (payload_len >= 1) {
      uint8_t n = payload[0];
      uint16_t p = 1;
      for (uint8_t i = 0; i < n && i < ROOM_STORAGE_MAX_POSTS_PER_CHUNK; ++i) {
        if (p + 4 + 4 + 32 + 1 > payload_len) break;
        RoomPost post = {};
        memcpy(&post.post_id, &payload[p], 4); p += 4;
        memcpy(&post.post_timestamp, &payload[p], 4); p += 4;
        post.author = mesh::Identity(&payload[p]); p += 32;
        uint8_t text_len = payload[p++];
        if (p + text_len > payload_len) break;
        text_len = min((uint8_t)ROOM_POST_TEXT_MAX, text_len);
        memcpy(post.text, &payload[p], text_len);
        post.text[text_len] = 0;
        p += text_len;
        r.posts[r.post_count++] = post;
      }
    }
    enqueueResult(r);
    return;
  }

  if (!pending) {
    return;
  }

  if (type == EXT_RESP_DONE || type == EXT_RESP_OK) {
    RoomStorageQueryResult r = {seq, pending->client_idx, ROOM_STORAGE_OK, true, 0, {}};
    enqueueResult(r);
    clearPending(seq);
    return;
  }

  if (type == EXT_RESP_BUSY) {
    RoomStorageQueryResult r = {seq, pending->client_idx, ROOM_STORAGE_BUSY, true, 0, {}};
    enqueueResult(r);
    clearPending(seq);
    return;
  }

  if (type == EXT_RESP_ERR_HISTORY_GAP) {
    RoomStorageQueryResult r = {seq, pending->client_idx, ROOM_STORAGE_HISTORY_GAP, true, 0, {}};
    enqueueResult(r);
    clearPending(seq);
    return;
  }

  if (type == EXT_RESP_CRC_ERROR) {
    RoomStorageQueryResult r = {seq, pending->client_idx, ROOM_STORAGE_CRC_ERROR, true, 0, {}};
    enqueueResult(r);
    clearPending(seq);
    return;
  }

  if (type == EXT_RESP_ERR) {
    RoomStorageQueryResult r = {seq, pending->client_idx, ROOM_STORAGE_ERR, true, 0, {}};
    enqueueResult(r);
    clearPending(seq);
    return;
  }
}

void ExternalMcuRoomStorage::loop() {
  pollFrames();

  uint32_t now = millis();
  if (now >= _last_ping_at + EXTERNAL_ROOM_STORAGE_PING_INTERVAL_MS) {
    _last_ping_at = now;
    uint16_t seq = nextSeq();
    sendFrame(EXT_CMD_PING, seq, NULL, 0);
  }

  if (_available && now > _last_rx_at + (EXTERNAL_ROOM_STORAGE_PING_INTERVAL_MS * 3)) {
    _available = false;
  }

  for (size_t i = 0; i < sizeof(_pending) / sizeof(_pending[0]); ++i) {
    if (_pending[i].active && now > _pending[i].started_at + EXTERNAL_ROOM_STORAGE_UART_TIMEOUT_MS) {
      RoomStorageQueryResult r = {_pending[i].seq, _pending[i].client_idx, ROOM_STORAGE_UNAVAILABLE, true, 0, {}};
      enqueueResult(r);
      memset(&_pending[i], 0, sizeof(_pending[i]));
      _available = false;
    }
  }
}

#endif
