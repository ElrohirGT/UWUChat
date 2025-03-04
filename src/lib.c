#include <stdint.h>

// Represents a string "slice"
//
// Only the creator of the original slice needs to free this memory!
struct UWU_String {
  // Contains all the data of this string
  // CAREFUL: It may or may not be a "null terminated string"!
  uint8_t *data;
  // The length of data that is considered to be "this string"
  uint32_t length;
};

// Represents a message on a given chat history.
//
// The ChatEntry should own it's memory! So it should receive a copy of
// `content` and `origin_username`.
struct UWU_ChatEntry {
  // The content of the message.
  struct UWU_String content;
  // The username of the person sending the message.
  struct UWU_String origin_username;
};

// Represents a message history of a certain chat
struct UWU_History {
  // A pointer to an array of `ChatEntry`.
  struct UWU_ChatEntry *messages;
  // The number of chat messages filling the array.
  uint32_t count;
  // How much memory is left in the array of `ChatEntry`.
  uint32_t capacity;
};
