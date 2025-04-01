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

// ======================================
//      UPDATE
// ======================================
// In this section manages all functionality related to Model management
// - websocket callbacks
// - User input (keystrokes, clicks)

void UWU_Update() {}

// Callback when WebSocket is opened
void on_open(ws_s *ws) {
  printf("Connected to WebSocket server!\n");
  UWU_ws_client = ws;
}

// Callback when a message is received
void on_message(ws_s *ws, fio_str_info_s msg, uint8_t is_text) {
  printf("Received: %.*s\n", (int)msg.len, msg.data);
}

// Callback when WebSocket is closed
void on_close(intptr_t uuid, void *udata) {
  printf("WebSocket connection closed.\n");
  UWU_ws_client = NULL;
}

// Send a message to the WebSocket server.
// user must be responsable of building the message.
void send_message(ws_s *ws, const fio_str_info_s *msg) {
  if (ws != NULL) {
    websocket_write(ws, *msg, 0);
  } else {
    printf("Cannot send message: WebSocket is not connected.\n");
  }
}

// UTILS
// ----------------

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
  str_info.len = strlen(UWU_TextInput.data);
  str_info.data = malloc(str_info.len);

  if (str_info.data) {
    memcpy(str_info.data, UWU_TextInput.data, str_info.len);
  } else {
    err = MALLOC_FAILED;
    return str_info;
  }

  return str_info;
}

// ======================================
//      VIEW
// ======================================
// LASTLY this section provides the methods to drow the current state on screen

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

//   VIEW
// ----------------------

Clay_RenderCommandArray CreateLayout(void) {
  Clay_BeginLayout();
  CLAY({.id = CLAY_ID("OuterContainer"),
        .layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                              .height = CLAY_SIZING_GROW(0)},
                   .padding = {16, 16, 16, 16},
                   .childGap = 16},
        .backgroundColor = COLOR_DARK_GREEN}) {}
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

  // Generate the auto layout for rendering
  double currentTime = GetTime();
  Clay_RenderCommandArray renderCommands = CreateLayout();
  BeginDrawing();
  ClearBackground(BLACK);
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
  size_t name_length = strlen(username) + 1;
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
  UWU_User group_chat = {.username = uwu_name, .status = ACTIVE};
  struct UWU_UserListNode group_chat_node =
      UWU_UserListNode_newWithValue(group_chat);

  // Initialize current active users list
  active_usernames = UWU_UserList_init();
  UWU_UserList_insertEnd(&active_usernames, &group_chat_node, err);
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
  if (websocket_connect(url, .on_open = on_open, .on_message = on_message,
                        .on_close = on_close) == -1) {
    printf("Failed to connect to WebSocket server.\n");
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
    UWU_PANIC("Unable to initialize server state...");
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
  Clay_Raylib_Initialize(1024, 768, "UWU Chat Client",
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
  }

  // pthread_join(fio_thread, NULL);
  deinitialize_server_state();
  return 0;
}