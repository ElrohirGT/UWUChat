#define CLAY_IMPLEMENTATION
#include "../deps/clay/clay.h"
#include "../deps/clay/renderers/raylib/clay_renderer_raylib.c"
#include "./lib.c"

/* Pthreads for running websocket client on background */
#include "pthread.h"

/* Include the core library */
#include "fiobj_hash.h"
#include "fiobj_str.h"
#include "fiobject.h"
#include "websockets.h"
#include <fio.h>

/* Include the TLS, CLI, FIOBJ and HTTP / WebSockets extensions */
#include <fio_cli.h>
#include <fio_tls.h>
#include <http.h>
#include <redis_engine.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The Websocket client is designed in a somewhat similiar way as elm
// having a model, view, update methods.

// ======================================
//      MODEL
// ======================================
//
// Current      Users    Current Chat
// User         List     History
// ┌────────┐   ┌─────┐  ┐───────────┐
// │        │   │     │  │           │
// └────────┘   ├─────┤  ┼───────────┤
//              │     │  │           │
//              │     │  ┼───────────┤
//              ├─────┤  │           │
//              │     │  ┼───────────┤
//              └─────┘  │           │
//                       ┴───────────┴
// The actual model is comprenhented by 3 data structures that
// are described as below.

// Current user being logged.
static UWU_User UWU_current_user;
// List of active users the client can send/receive messages from
static UWU_User UWU_group_chat;
static UWU_UserList active_usernames;
// Current chat history. BY DESIGN everytime the user changes its current chat,
// - The WHOLE chat history is fetched from the server, this is done ONLY ONCE,
// - After that, every new message received from the server is append it to the
// current object.
// - When set to NULL, no chat history is showed.
static UWU_ChatHistory *UWU_current_chat;
// Websocket client
static ws_s *UWU_ws_client = NULL;
// Text input, holds the current text that has been wrote by the user.
static UWU_String UWU_TextInput;

// The max quantity of messages a chat history can hold...
static const size_t MAX_MESSAGES_PER_CHAT = 100;
static const size_t MAX_CHARACTERS_INPUT = 254;
#define BACKSPACE 127 // Ascci code for backspace

// ===================
// UTILS
// ===================
void print_msg(fio_str_info_s *msg, char *prefix, char *action) {
  printf("%s %s: [ ", prefix, action);
  for (int i = 0; i < msg->len; i++) {
    printf("%c (%d)", msg->data[i], msg->data[i]);
    if (i + 1 < msg->len) {
      printf(", ");
    }
  }
  printf(" ]\n");
}

// Adds a new character to the end of the Textinput buffer.
void UWU_TextInput_append(char input) {
  int len = UWU_TextInput.length;
  if (len < MAX_CHARACTERS_INPUT) {
    UWU_TextInput.data[len] = input;
    UWU_TextInput.length++;
    UWU_TextInput.data[len + 1] = '\0';
  }
}

// Removes the last caracter from the textinput buffer by marking it as a null
// character.
void UWU_TextInput_remove_last() {
  int len = UWU_TextInput.length;
  if (len > 0) {
    UWU_TextInput.length--;
    UWU_TextInput.data[len] = '\0';
  }
}

// Clears the text input buffer
void UWU_TextInput_clear() {
  UWU_TextInput.length = 0;
  UWU_TextInput.data[0] = '\0';
}

// Creates a fio string from the current global textinput
// The caller owns the result.
fio_str_info_s UWU_TextInput_toFio(UWU_Err err) {

  fio_str_info_s str_info = {0};

  size_t username_length = UWU_current_chat->channel_name.length;
  size_t message_length = UWU_TextInput.length;
  size_t length = 4 + username_length + message_length;
  printf("Total length: %d\n", length);
  printf("username lenght: %d\n", username_length);
  printf("message leng: %d\n", message_length);
  char *data = malloc(length);

  if (data == NULL) {
    err = MALLOC_FAILED;
    return str_info;
  }

  data[0] = SEND_MESSAGE;
  data[1] = username_length;
  for (size_t i = 0; i < username_length; i++) {
    data[2 + i] = UWU_String_charAt(&UWU_current_chat->channel_name, i);
  }
  // a | a | a a a | a | a
  // 0 | 1 | 2 3 4 | 5 | 6
  // 1   2   3 4 5   5 | 7
  data[2 + username_length] = message_length;
  for (size_t i = 0; i < message_length; i++) {
    data[3 + username_length + i] = UWU_String_charAt(&UWU_TextInput, i);
  }
  str_info.data = data;
  str_info.len = length;

  return str_info;
}

// Converts a UWU_String to a Clay_String, useful.
Clay_String UWU_to_ClayString(UWU_String str) {
  return (Clay_String){.length = (int32_t)str.length, .chars = str.data};
}

// ======================================
//      UPDATE
// ======================================
// In this section manages all functionality related to Model management
// - websocket callbacks
// - UWU_
// - There are function per

// Send a message to the WebSocket server.
// user must be responsable of building the message.
void send_message(ws_s *ws, const fio_str_info_s *msg);

// Manages user input through keystrokes
void UWU_Update() {
  int key = GetCharPressed(); // Get character input from keyboard

  // Send a message to the server with the current input data.
  if (IsKeyPressed(KEY_ENTER)) {
    UWU_Err err = NO_ERROR;
    fio_str_info_s message = UWU_TextInput_toFio(err);
    if (err != NO_ERROR) {
      UWU_PANIC("Unable to allocate fio string before sending message");
    }
    send_message(UWU_ws_client, &message);
    free(message.data);
    UWU_TextInput_clear();
  } else if (IsKeyPressed(KEY_BACKSPACE)) {
    UWU_TextInput_remove_last();
  } else if (key > 0) {
    UWU_TextInput_append(key);
  }
}

// Sends a message requesting changing to Busy
void BusyBtnHandler(Clay_ElementId elementId, Clay_PointerData pointerInfo,
                    intptr_t userData) {
  if (pointerInfo.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    size_t username_length = UWU_current_user.username.length;
    size_t length = 3 + username_length;
    char *data = malloc(length);

    data[0] = CHANGE_STATUS;
    data[1] = username_length;

    for (size_t i = 0; i < username_length; i++) {
      data[2 + i] = UWU_String_charAt(&UWU_current_user.username, i);
    }

    data[length - 1] = BUSY;

    fio_str_info_s msg = {.data = data, .len = length};
    int err = websocket_write(UWU_ws_client, msg, 0);
    free(data);
  }
}

// Sends a message requesting changing to Active state
void ActiveBtnHandler(Clay_ElementId elementId, Clay_PointerData pointerInfo,
                      intptr_t userData) {
  if (pointerInfo.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    size_t username_length = UWU_current_user.username.length;
    size_t length = 3 + username_length;
    char *data = malloc(length);

    data[0] = CHANGE_STATUS;
    data[1] = username_length;

    for (size_t i = 0; i < username_length; i++) {
      data[2 + i] = UWU_String_charAt(&UWU_current_user.username, i);
    }

    data[length - 1] = ACTIVE;

    fio_str_info_s msg = {.data = data, .len = length};
    int err = websocket_write(UWU_ws_client, msg, 0);
    free(data);
  }
}

// Sends a message asking for the chat history of an specific user
void ChangeChatHandler(Clay_ElementId elementId, Clay_PointerData pointerInfo,
                       intptr_t userPointer) {

  if (pointerInfo.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    UWU_User *userValue = (UWU_User *)userPointer;
    char *tempUser = UWU_String_toCStr(&userValue->username);
    printf("Change Chat to %s!\n", tempUser);
    free(tempUser);

    UWU_User *user = (UWU_User *)userPointer;
    size_t length = 2 + user->username.length;
    char *data = malloc(length);
    if (data == NULL) {
      UWU_PANIC("COULD NOT INITIALIZE USER NAME ARRAY");
    }
    data[0] = GET_MESSAGES;
    data[1] = userValue->username.length;

    for (size_t i = 0; i < user->username.length; i++) {
      data[2 + i] = UWU_String_charAt(&userValue->username, i);
    }

    fio_str_info_s msg = {.data = data, .len = length};
    websocket_write(UWU_ws_client, msg, 0);
    free(data);
  }
}

// Callback when WebSocket is opened
void on_open(ws_s *ws) {
  printf("Connected to WebSocket server!\n");
  UWU_ws_client = ws;
}

// Callback when a message is received
void on_message(ws_s *ws, fio_str_info_s msg, uint8_t is_text) {
  print_msg(&msg, websocket_udata_get(ws), "Received");
  switch (msg.data[0]) {
  case ERROR: {
    fprintf(stderr, "Error: An error has ocurred! %d", msg.data[1]);
  } break;
  case LISTED_USERS: {

  } break;
  case GOT_USER: {

  } break;
  case REGISTERED_USER: {

  } break;
  case CHANGED_STATUS: {
    size_t username_length = msg.data[1];
    UWU_ConnStatus req_status = msg.data[2 + username_length];
    UWU_String req_username = {.data = &msg.data[2], .length = username_length};

    if (UWU_String_equal(&UWU_current_user.username, &req_username)) {
      UWU_current_user.status = req_status;
    } else {
      UWU_Bool found_it = FALSE;
      for (struct UWU_UserListNode *current = active_usernames.start;
           current != NULL; current = current->next) {
        if (current->is_sentinel) {
          continue;
        }

        if (UWU_String_equal(&current->data.username, &req_username)) {
          found_it = TRUE;
          current->data.status = req_status;
        }
      }

      if (!found_it) {
        UWU_Err err = NO_ERROR;
        UWU_User user = {.username = req_username, .status = req_status};
        struct UWU_UserListNode node = UWU_UserListNode_newWithValue(user);

        UWU_UserList_insertEnd(&active_usernames, &node, err);
        if (err != NO_ERROR) {
          UWU_PANIC("Fatal: Couldn't add username to active usernames!");
          return;
        }
      }
    }

  } break;
  case GOT_MESSAGE: {

  } break;
  case GOT_MESSAGES: {

  } break;
  default:
    fprintf(stderr, "Error: Unrecognized message!\n");
  }
}

// Callback when WebSocket is closed
void on_close(intptr_t uuid, void *udata) {
  printf("WebSocket connection closed.\n");
  UWU_ws_client = NULL;
}

void send_message(ws_s *ws, const fio_str_info_s *msg) {
  if (ws != NULL || msg->len > 0) {
    websocket_write(ws, *msg, 0);
  } else {
    printf("Cannot send message: WebSocket is not connected.\n");
  }
}

// ======================================
//      VIEW
// ======================================
// LASTLY this section provides the methods to drow the current state on
// screen

#define RAYLIB_VECTOR2_TO_CLAY_VECTOR2(vector)                                 \
  (Clay_Vector2) { .x = vector.x, .y = vector.y }

//   STYLES
// ----------------------
const uint32_t FONT_ID_BODY_24 = 0;
const uint32_t FONT_ID_BODY_16 = 1;

#define COLOR_WHITE (Clay_Color){255, 255, 255, 255}
#define COLOR_BLACK (Clay_Color){0, 0, 0, 255}
#define COLOR_GREY (Clay_Color){224, 224, 224, 255}
#define COLOR_DARK_GREEN (Clay_Color){7, 94, 84, 255}
#define COLOR_GREEN (Clay_Color){18, 140, 126, 255}
#define COLOR_LIGHT_GREEN (Clay_Color){220, 248, 198, 255}
#define COLOR_BACKGROUND (Clay_Color){236, 229, 221, 255}

#define COLOR_ACTIVE (Clay_Color){71, 209, 59, 255}
#define COLOR_BUSY (Clay_Color){66, 66, 212, 255}
#define COLOR_IDLE (Clay_Color){235, 155, 26, 255}

typedef struct {
  Clay_Vector2 clickOrigin;
  Clay_Vector2 positionOrigin;
  bool mouseDown;
} ScrollbarData;

ScrollbarData scrollbarData = {0};

//   COMPONENTES
// ----------------------
void UserCard(int index, UWU_User *user) {
  Clay_String a = UWU_to_ClayString(user->username);

  CLAY({.id = CLAY_IDI("user", index),
        .layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                              .height = CLAY_SIZING_FIXED(100)},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM,

                   .padding = {20, 0, 0, 0},
                   .childAlignment = {.x = CLAY_ALIGN_X_LEFT,
                                      .y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = Clay_Hovered() ? COLOR_GREY : COLOR_WHITE}) {
    Clay_OnHover(ChangeChatHandler, (intptr_t)user);
    CLAY_TEXT(a, CLAY_TEXT_CONFIG({.fontId = FONT_ID_BODY_24,
                                   .fontSize = 24,
                                   .textColor = COLOR_BLACK}));

    // Status indicator
    CLAY({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(20),
                                .height = CLAY_SIZING_FIXED(20)}},
          .cornerRadius = {10, 10, 10, 10},
          .backgroundColor =
              UWU_current_user.status == ACTIVE
                  ? COLOR_ACTIVE
                  : (UWU_current_user.status == BUSY ? COLOR_BUSY
                                                     : COLOR_IDLE)}) {}
  }
}

void ChatMessage(int index, UWU_ChatEntry *entry) {
  Clay_String username = UWU_to_ClayString(entry->origin_username);
  Clay_String content = UWU_to_ClayString(entry->content);
  CLAY({.layout = {.sizing = {.width = CLAY_SIZING_PERCENT(0.5),
                              .height = CLAY_SIZING_FIT(80)},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM,

                   .padding = {20, 0, 0, 0},
                   .childAlignment = {.x = CLAY_ALIGN_X_LEFT,
                                      .y = CLAY_ALIGN_Y_CENTER}},
        .cornerRadius = CLAY_CORNER_RADIUS(5),
        .backgroundColor = COLOR_WHITE}) {
    CLAY_TEXT(username, CLAY_TEXT_CONFIG({.fontId = FONT_ID_BODY_24,
                                          .fontSize = 24,
                                          .textColor = COLOR_BLACK}));
    CLAY_TEXT(content, CLAY_TEXT_CONFIG({.fontId = FONT_ID_BODY_24,
                                         .fontSize = 24,
                                         .textColor = COLOR_BLACK}));
  }
}

//   VIEW
// ----------------------

Clay_RenderCommandArray CreateLayout(void) {
  Clay_BeginLayout();
  CLAY({.id = CLAY_ID("OuterContainer"),
        .layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                              .height = CLAY_SIZING_GROW(0)},
                   .padding = {0, 0, 0, 0},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM},
        .backgroundColor = COLOR_BLACK}) {
    CLAY({.id = CLAY_ID("TopBar"),
          .layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                                .height = CLAY_SIZING_FIXED(60)},
                     .padding = {20, 20, 0, 0},
                     .childAlignment = {.x = CLAY_ALIGN_X_RIGHT,
                                        .y = CLAY_ALIGN_Y_CENTER},
                     .childGap = 16},
          .backgroundColor = COLOR_DARK_GREEN}) {
      CLAY({.id = CLAY_ID("ActiveBtn"),
            .layout = {.sizing = {.width = CLAY_SIZING_FIT(0),
                                  .height = CLAY_SIZING_PERCENT(0.7)},
                       .padding = CLAY_PADDING_ALL(3),
                       .childAlignment = {.x = CLAY_ALIGN_X_CENTER,
                                          .y = CLAY_ALIGN_Y_CENTER}},
            .backgroundColor = Clay_Hovered() ? COLOR_IDLE : COLOR_DARK_GREEN,
            .cornerRadius = CLAY_CORNER_RADIUS(10)}) {
        Clay_OnHover(ActiveBtnHandler, 0);
        CLAY_TEXT(CLAY_STRING("Active"),
                  CLAY_TEXT_CONFIG({.fontId = FONT_ID_BODY_24,
                                    .fontSize = 20,
                                    .textColor = COLOR_WHITE}));
      }
      CLAY({.id = CLAY_ID("BusyBtn"),
            .layout = {.sizing = {.width = CLAY_SIZING_FIT(0),
                                  .height = CLAY_SIZING_PERCENT(0.7)},
                       .padding = CLAY_PADDING_ALL(3),
                       .childAlignment = {.x = CLAY_ALIGN_X_CENTER,
                                          .y = CLAY_ALIGN_Y_CENTER}},
            .backgroundColor = Clay_Hovered() ? COLOR_IDLE : COLOR_DARK_GREEN,
            .cornerRadius = CLAY_CORNER_RADIUS(10)}) {

        Clay_OnHover(BusyBtnHandler, 0);
        CLAY_TEXT(CLAY_STRING("Busy"),
                  CLAY_TEXT_CONFIG({.fontId = FONT_ID_BODY_24,
                                    .fontSize = 20,
                                    .textColor = COLOR_WHITE}));
      }
    }
    CLAY({.id = CLAY_ID("Container"),
          .layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                                .height = CLAY_SIZING_GROW(0)}},
          .backgroundColor = COLOR_BACKGROUND}) {
      CLAY({
          .id = CLAY_ID("Sidebar"),
          .layout = {.sizing = {.width = CLAY_SIZING_PERCENT(0.20),
                                .height = CLAY_SIZING_GROW(0)},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .backgroundColor = COLOR_WHITE,
      }) {
        CLAY({.id = CLAY_ID("CurrentUser"),
              .layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                                    .height = CLAY_SIZING_FIXED(90)},
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,

                         .padding = {20, 0, 0, 0},
                         .childAlignment = {.x = CLAY_ALIGN_X_LEFT,
                                            .y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = COLOR_LIGHT_GREEN}) {
          CLAY_TEXT(UWU_to_ClayString(UWU_current_user.username),
                    CLAY_TEXT_CONFIG({.fontId = FONT_ID_BODY_24,
                                      .fontSize = 24,
                                      .textColor = COLOR_BLACK}));
          // Status indicator
          CLAY({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(20),
                                      .height = CLAY_SIZING_FIXED(20)}},
                .cornerRadius = {10, 10, 10, 10},
                .backgroundColor =
                    UWU_current_user.status == ACTIVE
                        ? COLOR_ACTIVE
                        : (UWU_current_user.status == BUSY ? COLOR_BUSY
                                                           : COLOR_IDLE)}) {}
        }
        // CONTACTS LIST
        CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                                    .height = CLAY_SIZING_GROW(0)},
                         .layoutDirection = CLAY_TOP_TO_BOTTOM},
              .scroll = {.vertical = true}}) {
          UserCard(0, &UWU_group_chat);
          int i = 1;
          for (struct UWU_UserListNode *current = active_usernames.start;
               current != NULL; current = current->next) {
            if (current->is_sentinel) {
              continue;
            }
            UserCard(i, &current->data);
          }
        }
      }
      CLAY({.id = CLAY_ID("Main"),
            .layout = {.sizing = {.width = CLAY_SIZING_PERCENT(0.80),
                                  .height = CLAY_SIZING_GROW(0)},
                       .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        // TEXT INPUT
        CLAY({.id = CLAY_ID("TextInputContainer"),
              .layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                                    .height = CLAY_SIZING_FIXED(60)},
                         .padding = {8, 8, 8, 8},
                         .childAlignment = {.x = CLAY_ALIGN_X_CENTER,
                                            .y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = COLOR_WHITE}) {

          CLAY({.id = CLAY_ID("TextInput"),
                .layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                                      .height = CLAY_SIZING_GROW(0)},
                           .padding = {16, 16, 0, 4},
                           .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
                .backgroundColor = COLOR_GREY,
                .cornerRadius = CLAY_CORNER_RADIUS(3)}) {
            CLAY_TEXT(UWU_to_ClayString(UWU_TextInput),
                      CLAY_TEXT_CONFIG({.fontId = FONT_ID_BODY_24,
                                        .fontSize = 20,
                                        .textColor = COLOR_BLACK}));
          }
        }
        // CHATS HISTORY
        CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                                    .height = CLAY_SIZING_GROW(0)},
                         .layoutDirection = CLAY_TOP_TO_BOTTOM},
              .scroll = {.vertical = true}}) {
          if (UWU_current_chat != NULL) {
            CLAY_TEXT(UWU_to_ClayString(UWU_current_chat->channel_name),
                      CLAY_TEXT_CONFIG({.fontId = FONT_ID_BODY_24,
                                        .fontSize = 20,
                                        .textColor = COLOR_BLACK}));
            UWU_ChatHistory_Iterator iter =
                UWU_ChatHistory_iter(UWU_current_chat);
            for (size_t i = iter.start; i < iter.end; i++) {
              UWU_ChatEntry entry = UWU_ChatHistory_get(
                  UWU_current_chat, i % UWU_current_chat->capacity);
              ChatMessage(i, &entry);
            }
          }
        }
      }
    }
  }
  return Clay_EndLayout();
}

void UWU_View(Font *fonts, UWU_User *current_user, UWU_UserList *active_user,
              UWU_ChatHistory *currentHistory) {
  Vector2 mouseWheelDelta = GetMouseWheelMoveV();
  float mouseWheelX = mouseWheelDelta.x;
  float mouseWheelY = mouseWheelDelta.y;

  //----------------------------------------------------------------------------------
  // Handle scroll containers
  Clay_Vector2 mousePosition =
      RAYLIB_VECTOR2_TO_CLAY_VECTOR2(GetMousePosition());
  Clay_SetPointerState(mousePosition,
                       IsMouseButtonDown(0) && !scrollbarData.mouseDown);
  Clay_SetLayoutDimensions(
      (Clay_Dimensions){(float)GetScreenWidth(), (float)GetScreenHeight()});
  if (!IsMouseButtonDown(0)) {
    scrollbarData.mouseDown = false;
  }

  Clay_UpdateScrollContainers(true, (Clay_Vector2){mouseWheelX, mouseWheelY},
                              GetFrameTime());

  // Generate the auto layout for rendering
  double currentTime = GetTime();
  Clay_RenderCommandArray renderCommands = CreateLayout();
  BeginDrawing();
  Clay_Raylib_Render(renderCommands, fonts);
  EndDrawing();
}

bool reinitializeClay = false;

void HandleClayErrors(Clay_ErrorData errorData) {
  printf("%s", errorData.errorText.chars);
  if (errorData.errorType == CLAY_ERROR_TYPE_ELEMENTS_CAPACITY_EXCEEDED) {
    reinitializeClay = true;
    Clay_SetMaxElementCount(Clay_GetMaxElementCount() * 2);
  } else if (errorData.errorType ==
             CLAY_ERROR_TYPE_TEXT_MEASUREMENT_CAPACITY_EXCEEDED) {
    reinitializeClay = true;
    Clay_SetMaxMeasureTextCacheWordCount(
        Clay_GetMaxMeasureTextCacheWordCount() * 2);
  }
}

// =================================
//  MAIN
// =================================

// Initializes client state...
void initialize_client_state(UWU_Err err, char *username) {
  // Current chat initalization
  UWU_current_chat = NULL;

  UWU_TextInput.data = (char *)malloc(MAX_CHARACTERS_INPUT + 1);
  if (UWU_TextInput.data == NULL) {
    err = MALLOC_FAILED;
    return;
  }
  UWU_TextInput.data[0] = '\0';
  UWU_TextInput.length = 0;

  // Create current user
  size_t name_length = strlen(username);
  char *username_data = malloc(name_length);
  if (NULL == username_data) {
    err = MALLOC_FAILED;
    return;
  }
  strcpy(username_data, username);
  UWU_String current_username = {.data = username_data, .length = name_length};

  // User initialization
  UWU_current_user.username = current_username;
  UWU_current_user.status = ACTIVE;

  // Create group chat user entry
  char *group_chat_name = "~";
  UWU_String uwu_name = {.data = group_chat_name, .length = 1};
  UWU_group_chat.username = uwu_name;
  UWU_group_chat.status = ACTIVE;

  // Initialize current active users list
  active_usernames = UWU_UserList_init(err);
  if (NO_ERROR != err) {
    return;
  }
  if (NO_ERROR != err) {
    return;
  }
}

// Clean client state...
void deinitialize_server_state() {
  fprintf(stderr, "Cleaning Text input...\n");
  UWU_String_freeWithMalloc(&UWU_TextInput);

  fprintf(stderr, "Cleaning Current User ...\n");
  UWU_User_free(&UWU_current_user);

  fprintf(stderr, "Cleaning User List...\n");
  UWU_UserList_deinit(&active_usernames);

  fprintf(stderr, "Cleaning Current Chat...\n");
  if (UWU_current_chat) {
    UWU_ChatHistory_deinit(UWU_current_chat);
    UWU_current_chat = NULL;
  }
}

// Secondary Thread executed on background for client websocket handling.
void *UWU_WebsocketClientLoop(void *arg) {
  char **params = (char **)arg;
  char *username = params[0]; // First parameter: username
  char *host = params[1];     // Second parameter: WebSocket URL

  printf("Username: %s\n", username);
  printf("Connecting to WebSocket server at: %s\n", host);

  char url[256];
  int err = snprintf(url, sizeof(url), "%s?name=%s", host, username);
  if (err < 0) {
    printf("Could not create connection url");
    deinitialize_server_state();
    exit(1);
  }
  printf("Username: %s\n", username);
  printf("Final connection URL: %s\n", &url);

  // Connect to WebSocket server
  if (-1 == websocket_connect(url, .on_open = on_open, .on_message = on_message,
                              .on_close = on_close)) {
    UWU_PANIC("Failed to connect to WebSocket server.\n");
    exit(1);
    return NULL;
  }

  // Start client event loop
  fio_start(.threads = 1);

  return NULL;
}

int main(int argc, char *argv[]) {
  // Ensure the user provides both the username and WebSocket URL as arguments
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <Username> <WebSocket_URL>\n", argv[0]);
    return 1;
  }

  if (strlen(argv[1]) > 255) {
    UWU_PANIC("Username to large!...");
    return 1;
  }

  char *username = argv[1]; // Get the username from the command line arguments
  char *ws_url =
      argv[2]; // Get the WebSocket URL from the command line arguments

  UWU_Err err = NO_ERROR;

  initialize_client_state(err, username);
  if (err != NO_ERROR) {
    UWU_PANIC("Unable to client state...");
    return 1;
  }

  // Prepare the parameters to pass to the thread (username and URL)
  char *params[] = {username, ws_url};

  // Initializing Websocket client on separate thread.
  pthread_t fio_thread;
  if (pthread_create(&fio_thread, NULL, UWU_WebsocketClientLoop,
                     (void *)params) != 0) {
    UWU_PANIC("Failed to create WebSocket thread");
    return 1;
  }

  // Clay Initialization
  uint64_t totalMemorySize = Clay_MinMemorySize();
  Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(
      totalMemorySize, malloc(totalMemorySize));
  Clay_Initialize(
      clayMemory,
      (Clay_Dimensions){(float)GetScreenWidth(), (float)GetScreenHeight()},
      (Clay_ErrorHandler){HandleClayErrors, 0});
  Clay_Raylib_Initialize(GetScreenWidth(), GetScreenHeight(), "UWU Chat Client",
                         FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE |
                             FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
  Font fonts[2];
  fonts[FONT_ID_BODY_24] =
      LoadFontEx("./src/resources/Roboto-Regular.ttf", 48, 0, 400);
  SetTextureFilter(fonts[FONT_ID_BODY_24].texture, TEXTURE_FILTER_BILINEAR);
  fonts[FONT_ID_BODY_16] =
      LoadFontEx("./src/resources/Roboto-Regular.ttf", 32, 0, 400);
  SetTextureFilter(fonts[FONT_ID_BODY_16].texture, TEXTURE_FILTER_BILINEAR);
  Clay_SetMeasureTextFunction(Raylib_MeasureText, fonts);

  // Main render loop
  while (!WindowShouldClose()) // Detect window close button or ESC key
  {
    if (reinitializeClay) {
      Clay_SetMaxElementCount(8192);
      totalMemorySize = Clay_MinMemorySize();
      clayMemory = Clay_CreateArenaWithCapacityAndMemory(
          totalMemorySize, malloc(totalMemorySize));
      Clay_Initialize(
          clayMemory,
          (Clay_Dimensions){(float)GetScreenWidth(), (float)GetScreenHeight()},
          (Clay_ErrorHandler){HandleClayErrors, 0});
      reinitializeClay = false;
    }
    UWU_View(fonts, &UWU_current_user, &active_usernames, UWU_current_chat);
    UWU_Update();
  }

  // pthread_join(fio_thread, NULL);
  websocket_close(UWU_ws_client);
  deinitialize_server_state();
  return 0;
}
