#include "http.h"
#include <stdio.h>
#include <websockets.h>

void on_open(ws_s *ws) {
  printf("Connected to WebSocket server!\n");
  printf("Sending message...\n");

  char data[] = {1};
  fio_str_info_s message = {.data = data, .len = 1};

  if (-1 == websocket_write(ws, message, 0)) {
    printf("FAILED TO SEND MESSAGE!");
  }
}

void on_message(ws_s *ws, fio_str_info_s msg, uint8_t is_text) {
  printf("Received: [ ");
  for (int i = 0; i < msg.len; i++) {
    printf("%c (%d)", msg.data[i], msg.data[i]);
    if (i + 1 < msg.len) {
      printf(", ");
    }
  }
  printf(" ]\n");
}

void on_close(intptr_t uuid, void *udata) {
  printf("WebSocket connection closed.\n");
}

int main() {
  char *url = "ws://127.0.0.1:8080/?name=Flavio";
  printf("Connecting to: %s\n", url);
  if (-1 == websocket_connect(url, .on_open = on_open, .on_message = on_message,
                              .on_close = on_close)) {
    printf("Failed to connect to WebSocket server.\n");
    return 1;
  }
  // Start client event loop
  fio_start(.threads = 1, .workers = 1);
  // sleep(5);
}
