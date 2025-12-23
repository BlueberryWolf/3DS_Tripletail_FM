#include "chat.h"
#include "jsmn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <citro2d.h>

// buffer configuration
#define MAX_JSON_TOKENS 16384  // recent_messages can be massive jesus (i should make the backend return less)

ChatStore chat_store;
LightLock chat_lock;
static uint32_t g_next_uid = 1;

uint64_t chat_get_time_ms(void) { return osGetTime(); }

static uint64_t parse_iso_date(const char *s) {
  if (!s || strlen(s) < 19)
    return chat_get_time_ms();

  struct tm t = {0};
  char buf[8];

  // YYYY
  memcpy(buf, s, 4);
  buf[4] = 0;
  t.tm_year = atoi(buf) - 1900;

  // MM
  memcpy(buf, s + 5, 2);
  buf[2] = 0;
  t.tm_mon = atoi(buf) - 1;

  // DD
  memcpy(buf, s + 8, 2);
  buf[2] = 0;
  t.tm_mday = atoi(buf);

  // HH
  memcpy(buf, s + 11, 2);
  buf[2] = 0;
  t.tm_hour = atoi(buf);

  // MM
  memcpy(buf, s + 14, 2);
  buf[2] = 0;
  t.tm_min = atoi(buf);

  // SS
  memcpy(buf, s + 17, 2);
  buf[2] = 0;
  t.tm_sec = atoi(buf);

  return (unsigned long long)mktime(&t) * 1000;
}

// jsmn helpers

static int jsoneq(const char *json, jsmntok_t *t, const char *s) {
  if (t->type == JSMN_STRING && (int)strlen(s) == t->end - t->start &&
      strncmp(json + t->start, s, t->end - t->start) == 0) {
    return 0;
  }
  return -1;
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

static void json_token_str(const char *json, jsmntok_t *t, char *out,
                           int max_len) {
  if (!t || t->type != JSMN_STRING) {
    if (t && t->type == JSMN_PRIMITIVE) {
       // primitive fallback
       int len = t->end - t->start;
       if (len >= max_len) len = max_len - 1;
       memcpy(out, json + t->start, len);
       out[len] = 0;
       return;
    } else {
      out[0] = 0;
      return;
    }
  }

  int src = t->start;
  int end = t->end;
  int dst = 0;
  
  while (src < end && dst < max_len - 1) {
    if (json[src] == '\\' && src + 1 < end) {
      if (json[src + 1] == 'u' && src + 5 < end) {
        // unicode escape \uXXXX
        uint32_t cp = (hex_digit(json[src + 2]) << 12) |
                      (hex_digit(json[src + 3]) << 8) |
                      (hex_digit(json[src + 4]) << 4) |
                      (hex_digit(json[src + 5]));
        src += 6;
        
        // encode utf-8
        if (cp < 0x80) {
            if (dst < max_len - 1) out[dst++] = (char)cp;
        } else if (cp < 0x800) {
            if (dst < max_len - 2) {
                out[dst++] = (char)(0xC0 | (cp >> 6));
                out[dst++] = (char)(0x80 | (cp & 0x3F));
            }
        } else if (cp < 0x10000) {
            if (dst < max_len - 3) {
                out[dst++] = (char)(0xE0 | (cp >> 12));
                out[dst++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out[dst++] = (char)(0x80 | (cp & 0x3F));
            }
        }
      } else {
          switch (json[src + 1]) {
            case '/':
            case '"':
            case '\\':
              out[dst++] = json[src + 1];
              src += 2;
              break;
            case 'n':
              out[dst++] = '\n';
              src += 2;
              break;
            case 't':
              out[dst++] = '\t';
              src += 2;
              break;
            case 'r':
              out[dst++] = '\r';
              src += 2;
              break;
            default:
              out[dst++] = json[src++];
              break;
          }
      }
    } else {
      out[dst++] = json[src++];
    }
  }
  out[dst] = '\0';
}

void chat_init(void) {
  memset(&chat_store, 0, sizeof(ChatStore));
  LightLock_Init(&chat_lock);
  srand(time(NULL));
  snprintf(chat_store.username, 32, "Floofer%03d", rand() % 1000);
}


void chat_set_username(const char *name) {
  if (name && name[0]) {
    strncpy(chat_store.username, name, 31);
    chat_store.username[31] = '\0';
  }
}

static int compare_msgs(const void *a, const void *b) {
  ChatMessage *ma = (ChatMessage *)a;
  ChatMessage *mb = (ChatMessage *)b;
  if (ma->receivedAt < mb->receivedAt)
    return -1;
  if (ma->receivedAt > mb->receivedAt)
    return 1;
  return 0;
}

static ChatMessage *get_msg_by_id(const char *id) {
  for (int i = 0; i < chat_store.count; i++) {
    if (strcmp(chat_store.messages[i].id, id) == 0)
      return &chat_store.messages[i];
  }
  return NULL;
}

static void add_message_to_store(ChatMessage *m, bool sort) {
  // deduplicate
  if (m->id[0]) {
      ChatMessage *existing = get_msg_by_id(m->id);
      if (existing) return;
  }

  if (chat_store.count >= MAX_MSGS) {
    memmove(&chat_store.messages[0], &chat_store.messages[1],
            sizeof(ChatMessage) * (MAX_MSGS - 1));
    chat_store.count--;
  }
  m->uid = g_next_uid++;
  chat_store.messages[chat_store.count++] = *m;
  if (sort) {
    qsort(chat_store.messages, chat_store.count, sizeof(ChatMessage),
          compare_msgs);
  }
}

// action shit

void chat_send_message(const char *text, const char *replyTo) {
  if (chat_store.netCtx && chat_store.isConnected && text && text[0]) {
    char payload[1024];
    char esc_text[512];
    int p = 0;
    for (int i = 0; text[i] && p < 500; i++) {
      if (text[i] == '"') {
        esc_text[p++] = '\\';
        esc_text[p++] = '"';
      } else if (text[i] == '\\') {
        esc_text[p++] = '\\';
        esc_text[p++] = '\\';
      } else
        esc_text[p++] = text[i];
    }
    esc_text[p] = 0;

    snprintf(payload, sizeof(payload),
             "42[\"chat_message\",{\"user\":\"%s\",\"text\":\"%s\",\"user_"
             "color\":\"%s\",\"replyTo\":%s}]",
             chat_store.username, esc_text, chat_store.userColor,
             replyTo ? replyTo : "null");

    net_send_ws(chat_store.netCtx, payload);
  }
}

void chat_send_typing(void) {
  if (chat_store.netCtx && chat_store.isConnected) {
    char payload[512];
    snprintf(payload, sizeof(payload), "42[\"typing\",{\"user\":\"%s\"}]",
             chat_store.username);
    net_send_ws(chat_store.netCtx, payload);
  }
}

void chat_add_reaction(const char *msgId, const char *emoji) {
  if (chat_store.netCtx && chat_store.isConnected) {
    char payload[512];
    snprintf(payload, sizeof(payload),
             "42[\"add_reaction\",{\"messageId\":\"%s\",\"emoji\":\"%s\","
             "\"user\":\"%s\"}]",
             msgId, emoji, chat_store.username);
    net_send_ws(chat_store.netCtx, payload);
  }
}

// events

static int skip_value(jsmntok_t *tokens, int current, int num_tokens) {
  int val_end = tokens[current].end;
  current++;
  while (current < num_tokens && tokens[current].start > 0 &&
         tokens[current].start < val_end) {
    current++;
  }
  return current;
}

static void handle_chat_message(const char *json, jsmntok_t *tokens,
                                int obj_idx, int num_tokens, bool live) {
  ChatMessage m = {0};
  int num_fields = tokens[obj_idx].size;
  int current = obj_idx + 1;

  char timestamp[64] = "";

  for (int i = 0; i < num_fields; i++) {
    if (current >= num_tokens)
      break;

    if (jsoneq(json, &tokens[current], "id") == 0) {
      json_token_str(json, &tokens[current + 1], m.id, 48);
    } else if (jsoneq(json, &tokens[current], "user") == 0) {
      json_token_str(json, &tokens[current + 1], m.user, 32);
    } else if (jsoneq(json, &tokens[current], "text") == 0) {
      json_token_str(json, &tokens[current + 1], m.text, 512);
    } else if (jsoneq(json, &tokens[current], "user_color") == 0) {
      json_token_str(json, &tokens[current + 1], m.user_color, 16);
    } else if (jsoneq(json, &tokens[current], "timestamp") == 0) {
      json_token_str(json, &tokens[current + 1], timestamp, 64);
    }

    current = skip_value(tokens, current + 1, num_tokens);
  }

  if (timestamp[0]) {
    m.receivedAt = parse_iso_date(timestamp);
  } else {
    m.receivedAt = chat_get_time_ms();
  }

  // parsed and cached
  if (m.user_color[0] && strlen(m.user_color) >= 6) {
    const char *p = (m.user_color[0] == '#') ? m.user_color + 1 : m.user_color;
    
    if (strlen(p) >= 6) {
      unsigned int r = 0, g = 0, b = 0;
      
      for (int i = 0; i < 6; i++) {
        int v = 0;
        char c = p[i];
        if (c >= '0' && c <= '9')
          v = c - '0';
        else if (c >= 'a' && c <= 'f')
          v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
          v = c - 'A' + 10;
        
        if (i < 2)
          r = (r << 4) | v;
        else if (i < 4)
          g = (g << 4) | v;
        else
          b = (b << 4) | v;
      }
      m.user_color_parsed = C2D_Color32(r, g, b, 255);
    } else {
      m.user_color_parsed = C2D_Color32(0, 255, 255, 255); // default cyan
    }
  } else {
    m.user_color_parsed = C2D_Color32(0, 255, 255, 255);   // default cyan
  }

  LightLock_Lock(&chat_lock);
  add_message_to_store(&m, false);
  LightLock_Unlock(&chat_lock);
}

static void handle_reaction_update(const char *json, jsmntok_t *tokens,
                                   int obj_idx, int num_tokens, bool added) {
  char msgId[48] = "", emoji[16] = "", user[32] = "";
  int num = tokens[obj_idx].size;
  int cur = obj_idx + 1;

  for (int i = 0; i < num; i++) {
    if (cur >= num_tokens)
      break;
    if (jsoneq(json, &tokens[cur], "messageId") == 0)
      json_token_str(json, &tokens[cur + 1], msgId, 48);
    else if (jsoneq(json, &tokens[cur], "emoji") == 0)
      json_token_str(json, &tokens[cur + 1], emoji, 16);
    else if (jsoneq(json, &tokens[cur], "user") == 0)
      json_token_str(json, &tokens[cur + 1], user, 32);

    cur = skip_value(tokens, cur + 1, num_tokens);
  }

  LightLock_Lock(&chat_lock);
  ChatMessage *m = get_msg_by_id(msgId);
  if (!m) {
    LightLock_Unlock(&chat_lock);
    return;
  }

  int r_idx = -1;
  for (int i = 0; i < m->reaction_count; i++) {
    if (strcmp(m->reactions[i].emoji, emoji) == 0) {
      r_idx = i;
      break;
    }
  }

  if (added) {
    if (r_idx == -1 && m->reaction_count < MAX_REACTIONS) {
      r_idx = m->reaction_count++;
      snprintf(m->reactions[r_idx].emoji, sizeof(m->reactions[r_idx].emoji), "%s", emoji);
      m->reactions[r_idx].user_count = 0;
    }
    if (r_idx != -1) {
      Reaction *r = &m->reactions[r_idx];
      bool exists = false;
      for (int u = 0; u < r->user_count; u++)
        if (strcmp(r->users[u], user) == 0)
          exists = true;
      if (!exists && r->user_count < MAX_REACT_USERS) {
        snprintf(r->users[r->user_count++], sizeof(r->users[0]), "%s", user);
      }
    }
  } else {
    if (r_idx != -1) {
      Reaction *r = &m->reactions[r_idx];
      for (int u = 0; u < r->user_count; u++) {
        if (strcmp(r->users[u], user) == 0) {
          for (int k = u; k < r->user_count - 1; k++)
            strcpy(r->users[k], r->users[k + 1]);
          r->user_count--;
          break;
        }
      }
      if (r->user_count == 0) {
        for (int k = r_idx; k < m->reaction_count - 1; k++)
          m->reactions[k] = m->reactions[k + 1];
        m->reaction_count--;
      }
    }
  }
  LightLock_Unlock(&chat_lock);
}

static void handle_typing(const char *json, jsmntok_t *tokens, int idx,
                          int num_tokens) {
  char user[32] = "";
  int num = tokens[idx].size;
  int cur = idx + 1;
  for (int i = 0; i < num; i++) {
    if (cur >= num_tokens)
      break;
    if (jsoneq(json, &tokens[cur], "user") == 0)
      json_token_str(json, &tokens[cur + 1], user, 32);
    cur = skip_value(tokens, cur + 1, num_tokens);
  }

  if (user[0]) {
    bool found = false;
    for (int i = 0; i < chat_store.typer_count; i++) {
      if (strcmp(chat_store.typers[i].user, user) == 0) {
        chat_store.typers[i].last_typed = chat_get_time_ms();
        found = true;
        break;
      }
    }
    if (!found && chat_store.typer_count < MAX_TYPERS) {
      snprintf(chat_store.typers[chat_store.typer_count].user, sizeof(chat_store.typers[0].user), "%s", user);
      chat_store.typers[chat_store.typer_count].last_typed = chat_get_time_ms();
      chat_store.typer_count++;
    }
  }
}

static void handle_message_deleted(const char *json, jsmntok_t *t, int idx,
                                   int num_tokens) {
  char id[48] = "";

  // if "message_deleted", "123" -> JSMN_STRING
  if (t[idx].type == JSMN_STRING || t[idx].type == JSMN_PRIMITIVE) {
    json_token_str(json, &t[idx], id, 48);
  } else if (t[idx].type == JSMN_OBJECT) {
    // {"id": "123"}
    int num_fields = t[idx].size;
    int cur = idx + 1;
    for (int i = 0; i < num_fields; i++) {
      if (cur >= num_tokens)
        break;
      if (jsoneq(json, &t[cur], "id") == 0) {
        json_token_str(json, &t[cur + 1], id, 48);
        break;
      }
      cur = skip_value(t, cur + 1, num_tokens);
    }
  }

  if (id[0]) {
    ChatMessage *m = get_msg_by_id(id);
    if (m) {
      m->deleted = true;
    }
  }
}

void chat_process_packet(char *json_payload, size_t len) {
  // token buffer for json parsing
  static jsmntok_t *tokens = NULL;
  if (!tokens)
    tokens = malloc(sizeof(jsmntok_t) * MAX_JSON_TOKENS);
  if (!tokens)
    return;

  jsmn_parser p;
  jsmn_init(&p);
  int ret = jsmn_parse(&p, json_payload, len, tokens, MAX_JSON_TOKENS);

  if (ret < 0) {
    printf("JSMN Parse Error: %d\n", ret);
    return;
  }

  int num_toks = ret;

  if (num_toks > 0 && tokens[0].type == JSMN_ARRAY && tokens[0].size >= 2) {
    char event[64];
    json_token_str(json_payload, &tokens[1], event, 64);

    // tokens[2] is payload
    if (strcmp(event, "chat_message") == 0) {
      if (tokens[2].type == JSMN_OBJECT) {
        handle_chat_message(json_payload, tokens, 2, num_toks, true);
      }
    } else if (strcmp(event, "recent_messages") == 0 &&
               tokens[2].type == JSMN_ARRAY) {
      int count = tokens[2].size;
      int cur = 3;
      int skip = (count > 20) ? (count - 20) : 0;
      
      for (int i = 0; i < count; i++) {
        if (cur >= num_toks)
          break;
          
        if (i < skip) {
            // skip this message object without parsing
            if (tokens[cur].type == JSMN_OBJECT) {
                 cur = skip_value(tokens, cur, num_toks);
            } else {
                 cur++;
            }
            continue;
        }
        
        if (tokens[cur].type == JSMN_OBJECT) {
          handle_chat_message(json_payload, tokens, cur, num_toks, false);
          cur = skip_value(tokens, cur, num_toks);
        } else {
          cur++;
        }
      }
      LightLock_Lock(&chat_lock);
      qsort(chat_store.messages, chat_store.count, sizeof(ChatMessage),
            compare_msgs);
      LightLock_Unlock(&chat_lock);

    } else if (strcmp(event, "message_deleted") == 0) {
      handle_message_deleted(json_payload, tokens, 2, num_toks);
    } else if (strcmp(event, "reaction_added") == 0) {
      handle_reaction_update(json_payload, tokens, 2, num_toks, true);
    } else if (strcmp(event, "reaction_removed") == 0) {
      handle_reaction_update(json_payload, tokens, 2, num_toks, false);
    } else if (strcmp(event, "typing") == 0) {
      handle_typing(json_payload, tokens, 2, num_toks);
    }
  }

  // static buffer, do not free
}

void chat_clean_typers(void) {
  uint64_t now = chat_get_time_ms();
  for (int i = 0; i < chat_store.typer_count; i++) {
    if (now - chat_store.typers[i].last_typed > TYPING_TIMEOUT) {
      for (int k = i; k < chat_store.typer_count - 1; k++)
        chat_store.typers[k] = chat_store.typers[k + 1];
      chat_store.typer_count--;
      i--;
    }
  }
}
