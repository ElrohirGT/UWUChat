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
// - After that, every new message received from the server is append it to the current object.
static UWU_ChatHistory UWU_current_chat;

// The max quantity of messages a chat history can hold...
const size_t MAX_MESSAGES_PER_CHAT = 100;

// Websocket client
static ws_s *UWU_ws_client = NULL;

// ======================================
//      UPDATE
// ======================================
// In this section manages all functionality related to Model management
// - websocket callbacks
// - User input (keystrokes, clicks)

void UWU_Update() {

}

// Callback when WebSocket is opened
void on_open(ws_s *ws) {
    printf("Connected to WebSocket server!\n");
    fio_str_info_s msg = {.data = "Hello, WebSocket!", .len = 17};
    websocket_write(ws, msg, 1);  // Send a text message
}

// Callback when a message is received
void on_message(ws_s *ws, fio_str_info_s msg, uint8_t is_text) {
    printf("Received: %.*s\n", (int)msg.len, msg.data);
}

// Callback when WebSocket is closed
void on_close(intptr_t uuid, void *udata) {
    printf("WebSocket connection closed.\n");
}

// ======================================
//      VIEW
// ======================================
// LASTLY this section provides the methods to drow the current state on screen

//   STYLES
// ----------------------
const uint32_t FONT_ID_BODY_24 = 0;
const uint32_t FONT_ID_BODY_16 = 1;

#define COLOR_WHITE (Clay_Color) {255, 255, 255, 255}
#define COLOR_BLACK (Clay_Color) {0, 0, 0, 255}
#define COLOR_GREY (Clay_Color) {224, 224, 224, 255}
#define COLOR_DARK_GREEN (Clay_Color) {7, 94, 84, 255}
#define COLOR_LIGHT_GREEN (Clay_Color) {220, 248, 198, 255}
#define COLOR_BACKGROUND (Clay_Color) {236, 229, 221, 255}

#define COLOR_ACTIVE (Clay_Color) {71, 209, 59, 255}
#define COLOR_BUSY (Clay_Color) {66, 66, 212, 255}
#define COLOR_IDLE (Clay_Color) {235, 155, 26, 255}

//   COMPONENTES
// ----------------------



//   VIEW
// ----------------------

void UWU_View(
    Font* fonts, 
    UWU_User* current_user,
    UWU_UserList* active_user,
    UWU_ChatHistory* currentHistory){

}

bool reinitializeClay = false;

void HandleClayErrors(Clay_ErrorData errorData) {
    printf("%s", errorData.errorText.chars);
    if (errorData.errorType == CLAY_ERROR_TYPE_ELEMENTS_CAPACITY_EXCEEDED) {
        reinitializeClay = true;
        Clay_SetMaxElementCount(Clay_GetMaxElementCount() * 2);
    } else if (errorData.errorType == CLAY_ERROR_TYPE_TEXT_MEASUREMENT_CAPACITY_EXCEEDED) {
        reinitializeClay = true;
        Clay_SetMaxMeasureTextCacheWordCount(Clay_GetMaxMeasureTextCacheWordCount() * 2);
    }
}

// =================================
//  MAIN
// =================================

void *start_fio_loop(void *arg) {
    char **params = (char **)arg;
    char *username = params[0]; // First parameter: username
    char *host = params[1];      // Second parameter: WebSocket URL

    printf("Username: %s\n", username);
    printf("Connecting to WebSocket server at: %s\n", host);

    char url[256];
    snprintf(url, sizeof(url), "%s?name=%s", host, username);
    printf("Username: %s\n", username);
    
    // Connect to WebSocket server
    if (websocket_connect(url, 
        .on_open = on_open,
        .on_message = on_message,
        .on_close = on_close) == -1) {
        printf("Failed to connect to WebSocket server.\n");
        return NULL;
    }

    // Start facil.io event loop
    fio_start(.threads = 1);  // Run facil.io event loop

    return NULL;
}

int main(int argc, char *argv[]) {

    // Ensure the user provides both the username and WebSocket URL as arguments
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <Username> <WebSocket_URL>\n", argv[0]);
        return 1;
    }

    char *username = argv[1];  // Get the username from the command line arguments
    char *ws_url = argv[2];    // Get the WebSocket URL from the command line arguments
                               //
    // Prepare the parameters to pass to the thread (username and URL)
    char *params[] = {username, ws_url};

    // Initializing Websocket client on separate thread.
    pthread_t fio_thread;
    if (pthread_create(&fio_thread, NULL, start_fio_loop, (void *)params) != 0) {
        UWU_PANIC("Failed to create WebSocket thread");
        return 1;
    }

    // uint64_t totalMemorySize = Clay_MinMemorySize();
    // Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(totalMemorySize, malloc(totalMemorySize));
    // Clay_Initialize(clayMemory, (Clay_Dimensions) { (float)GetScreenWidth(), (float)GetScreenHeight() }, (Clay_ErrorHandler) { HandleClayErrors, 0 });
    // Clay_Raylib_Initialize(1024, 768, "UWU Chat", FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);

    // Font fonts[2];
    // fonts[FONT_ID_BODY_24] = LoadFontEx("src/resources/Roboto-Regular.ttf", 48, 0, 400);
	// SetTextureFilter(fonts[FONT_ID_BODY_24].texture, TEXTURE_FILTER_BILINEAR);
    // fonts[FONT_ID_BODY_16] = LoadFontEx("src/resources/Roboto-Regular.ttf", 32, 0, 400);
    // SetTextureFilter(fonts[FONT_ID_BODY_16].texture, TEXTURE_FILTER_BILINEAR);
    // Clay_SetMeasureTextFunction(Raylib_MeasureText, fonts);
    
    
    //--------------------------------------------------------------------------------------

    // // Main Render Loop
    // while (!WindowShouldClose())    // Detect window close button or ESC key
    // {
    //     if (reinitializeClay) {
    //         Clay_SetMaxElementCount(8192);
    //         totalMemorySize = Clay_MinMemorySize();
    //         clayMemory = Clay_CreateArenaWithCapacityAndMemory(totalMemorySize, malloc(totalMemorySize));
    //         Clay_Initialize(
    //             clayMemory, 
    //             (Clay_Dimensions) { (float)GetScreenWidth(),
    //             (float)GetScreenHeight() },
    //             (Clay_ErrorHandler) { HandleClayErrors, 0 });
    //         reinitializeClay = false;
    //     }
        
    // }

    // Clean up (not actually reached in this example)
    pthread_join(fio_thread, NULL);
    return 0;
}