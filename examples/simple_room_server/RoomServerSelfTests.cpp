#include "ChatCommandParser.h"
#include "RoomStorage.h"

#if defined(ROOM_SERVER_SELF_TESTS)

static bool testCommandParsing() {
  ParsedRoomCommand c1 = ChatCommandParser::parse("!help", 25);
  if (!(c1.is_command && c1.valid && c1.type == ROOM_CMD_HELP)) return false;

  ParsedRoomCommand c2 = ChatCommandParser::parse("!resend last 7", 25);
  if (!(c2.valid && c2.type == ROOM_CMD_LAST && c2.limit == 7)) return false;

  ParsedRoomCommand c3 = ChatCommandParser::parse(".after 123", 25);
  if (!(c3.valid && c3.type == ROOM_CMD_AFTER && c3.post_id == 123)) return false;

  ParsedRoomCommand c4 = ChatCommandParser::parse("!resend since 2026-05-12T16:32:00Z", 25);
  if (!(c4.valid && c4.type == ROOM_CMD_SINCE && c4.since_timestamp > 0)) return false;

  ParsedRoomCommand c5 = ChatCommandParser::parse("!resend last nope", 25);
  if (!c5.is_command || c5.valid) return false;

  return true;
}

static bool testLocalStorageQueries() {
  LocalRoomStorage storage;
  storage.begin();

  RoomPost p = {};
  p.author = mesh::Identity();

  for (uint32_t i = 1; i <= 5; ++i) {
    p.post_id = i;
    p.post_timestamp = i;
    snprintf(p.text, sizeof(p.text), "post %lu", (unsigned long)i);
    if (!storage.appendPost(p)) return false;
  }

  if (!storage.getLast(1, 0, 3)) return false;
  RoomStorageQueryResult r = {};
  uint8_t seen = 0;
  while (storage.pollQueryResult(r)) {
    seen += r.post_count;
    if (r.done) break;
  }
  if (seen != 3) return false;

  if (!storage.getAfterId(2, 0, 2, 25)) return false;
  seen = 0;
  while (storage.pollQueryResult(r)) {
    seen += r.post_count;
    if (r.done) break;
  }
  if (seen != 3) return false;

  if (!storage.getAfterId(3, 0, 0, 25)) return false;
  while (storage.pollQueryResult(r)) {
    if (r.done) break;
  }

  if (!storage.getAfterId(4, 0, 1, 25)) return false;
  while (storage.pollQueryResult(r)) {
    if (r.done) break;
  }

  return true;
}

bool runRoomServerSelfTests() {
  return testCommandParsing() && testLocalStorageQueries();
}

#endif
