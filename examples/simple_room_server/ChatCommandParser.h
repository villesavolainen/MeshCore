#pragma once

#include <Arduino.h>

enum RoomCommandType : uint8_t {
  ROOM_CMD_NONE = 0,
  ROOM_CMD_HELP,
  ROOM_CMD_LAST,
  ROOM_CMD_AFTER,
  ROOM_CMD_SINCE,
};

struct ParsedRoomCommand {
  bool is_command;
  bool valid;
  RoomCommandType type;
  uint16_t limit;
  uint32_t post_id;
  uint32_t since_timestamp;
  char error[48];
};

class ChatCommandParser {
public:
  static ParsedRoomCommand parse(const char* text, uint16_t max_limit);
  static bool parseIso8601Utc(const char* text, uint32_t& out_timestamp);
};
