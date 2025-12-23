#pragma once
#include "net.h"
#include <stdbool.h>
#include <stdint.h>


#define MAX_MSGS 500
#define MAX_TYPERS 10
#define TYPING_TIMEOUT 3000
#define MAX_REACTIONS 8
#define MAX_REACT_USERS 5
#include <citro2d.h>

typedef struct {
  char emoji[16];
  char users[MAX_REACT_USERS][32];
  int user_count;
} Reaction;

typedef struct {
  uint32_t uid;
  char id[48];
  char user[32];
  char text[512];
  char user_color[16];
  u32 user_color_parsed;  // cached parsed color
  char replyTo[48];
  uint64_t receivedAt; // milliseconds

  Reaction reactions[MAX_REACTIONS];
  int reaction_count;

  bool deleted;

  // rendering cache
  C2D_Text msgText;
  C2D_Text userText;
  bool text_cached;
} ChatMessage;

typedef struct {
  char user[32];
  uint64_t last_typed;
} Typer;

typedef struct {
  ChatMessage messages[MAX_MSGS];
  int count;

  Typer typers[MAX_TYPERS];
  int typer_count;

  bool isConnected;
  char username[32];
  char userColor[16];

  SecureCtx *netCtx;
} ChatStore;

extern ChatStore chat_store;
extern LightLock chat_lock;

void chat_init(void);
void chat_exit(void);
void chat_process_packet(char *json_payload, size_t len);

void chat_set_username(const char *name);

void chat_clean_typers(void);

// actions
void chat_send_message(const char *text, const char *replyTo);
void chat_send_typing(void);
void chat_add_reaction(const char *msgId, const char *emoji);
void chat_remove_reaction(const char *msgId, const char *emoji);

// utils
uint64_t chat_get_time_ms(void);
