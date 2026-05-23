#include "ChatCommandParser.h"

#include <RTClib.h>
#include <ctype.h>
#include <helpers/TxtDataHelpers.h>

static void setError(ParsedRoomCommand& out, const char* err) {
  out.valid = false;
  StrHelper::strncpy(out.error, err, sizeof(out.error));
}

static bool parseUint32(const char* s, uint32_t& out) {
  if (!s || !*s) return false;
  uint64_t v = 0;
  for (const char* p = s; *p; ++p) {
    if (!isdigit((unsigned char)*p)) return false;
    v = (v * 10) + (uint32_t)(*p - '0');
    if (v > 0xFFFFFFFFULL) return false;
  }
  out = (uint32_t)v;
  return true;
}

static bool parseUint16(const char* s, uint16_t& out) {
  uint32_t v;
  if (!parseUint32(s, v) || v > 0xFFFF) return false;
  out = (uint16_t)v;
  return true;
}

static char* nextToken(char*& p) {
  while (*p == ' ') ++p;
  if (*p == 0) return NULL;
  char* start = p;
  while (*p && *p != ' ') ++p;
  if (*p) {
    *p = 0;
    ++p;
  }
  return start;
}

bool ChatCommandParser::parseIso8601Utc(const char* text, uint32_t& out_timestamp) {
  if (!text || strlen(text) != 20) {
    return false;
  }

  if (text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':' || text[19] != 'Z') {
    return false;
  }

  auto read2 = [&](int ofs, int& out) -> bool {
    if (!isdigit((unsigned char)text[ofs]) || !isdigit((unsigned char)text[ofs + 1])) return false;
    out = (text[ofs] - '0') * 10 + (text[ofs + 1] - '0');
    return true;
  };

  int year = 0;
  for (int i = 0; i < 4; ++i) {
    if (!isdigit((unsigned char)text[i])) return false;
    year = year * 10 + (text[i] - '0');
  }

  int month, day, hh, mm, ss;
  if (!read2(5, month) || !read2(8, day) || !read2(11, hh) || !read2(14, mm) || !read2(17, ss)) {
    return false;
  }

  if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31 || hh > 23 || mm > 59 || ss > 59) {
    return false;
  }

  DateTime dt((uint16_t)year, (uint8_t)month, (uint8_t)day, (uint8_t)hh, (uint8_t)mm, (uint8_t)ss);
  if (!dt.isValid()) {
    return false;
  }

  out_timestamp = dt.unixtime();
  return true;
}

ParsedRoomCommand ChatCommandParser::parse(const char* text, uint16_t max_limit) {
  ParsedRoomCommand out = {};
  out.is_command = false;
  out.valid = false;
  out.type = ROOM_CMD_NONE;
  out.limit = 0;
  out.post_id = 0;
  out.since_timestamp = 0;
  out.error[0] = 0;

  if (!text) {
    return out;
  }

  while (*text == ' ') ++text;
  if (*text != '!' && *text != '.') {
    return out;
  }

  out.is_command = true;

  char buffer[96];
  StrHelper::strncpy(buffer, text + 1, sizeof(buffer));
  char* p = buffer;

  char* root = nextToken(p);
  if (!root) {
    setError(out, "empty command");
    return out;
  }

  if (strcmp(root, "help") == 0) {
    out.valid = true;
    out.type = ROOM_CMD_HELP;
    return out;
  }

  char* op = root;
  if (strcmp(root, "resend") == 0) {
    op = nextToken(p);
    if (!op) {
      setError(out, "usage: !resend last|after|since ...");
      return out;
    }
  }

  if (strcmp(op, "last") == 0) {
    char* n = nextToken(p);
    uint16_t limit;
    if (!n || !parseUint16(n, limit) || limit == 0) {
      setError(out, "usage: !resend last <n>");
      return out;
    }
    out.valid = true;
    out.type = ROOM_CMD_LAST;
    out.limit = (limit > max_limit) ? max_limit : limit;
    return out;
  }

  if (strcmp(op, "after") == 0) {
    char* n = nextToken(p);
    uint32_t post_id;
    if (!n || !parseUint32(n, post_id) || post_id == 0) {
      setError(out, "usage: !resend after <post_id>");
      return out;
    }
    out.valid = true;
    out.type = ROOM_CMD_AFTER;
    out.post_id = post_id;
    out.limit = max_limit;
    return out;
  }

  if (strcmp(op, "since") == 0) {
    char* ts = nextToken(p);
    if (!ts || !parseIso8601Utc(ts, out.since_timestamp)) {
      setError(out, "usage: !resend since 2026-05-12T16:32:00Z");
      return out;
    }
    out.valid = true;
    out.type = ROOM_CMD_SINCE;
    out.limit = max_limit;
    return out;
  }

  setError(out, "unknown command");
  return out;
}
