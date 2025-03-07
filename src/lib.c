#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* *****************************************************************************
Welcum
***************************************************************************** */

/*
In C we can't return tuples or two parameters at the same time, an alternative
would be to make a `Result` type kind of like Rust, but that would involve using
a lot of (void*).

This is bad... (why you ask? Think of `void*` as `any` in
typescript). So a nice middle ground between not using void* (so we can keep
some static analysis on our code) and simply ignoring errors is passing a
parameter that changes it's value in case of an error.

Please, **ALWAYS CHECK THE ERR PARAMETER** after calling a function that
requires it! It's like programming in Go but for C...
*/
typedef size_t *UWU_ERR;
// Represents the absence of an error
static const UWU_ERR NO_ERROR = NULL;

typedef int bool;
static const bool TRUE = 1;
static const bool FALSE = 0;

// A panic represents an irrecoverable error.
//
// The program somehow got into an irrecoverable state and there's no other
// option other than to panic, because continuing would hide a bug!
//
// Common examples of appropriate places to panic include:
// * Accessing items out of bounds.
// * Trying to close a connection that has already been closed.
// * Trying to free memory already freed.
void UWU_PANIC(const char *format, ...) {
  va_list args;

  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  exit(1);
}

/* *****************************************************************************
Enums
***************************************************************************** */

// Represents all the possible values for a connection status.
typedef enum {
  DISCONNETED,
  ACTIVE,
  BUSY,
  INACTIVE,
} UWU_ConnStatus;

// Represents all the "type codes" of messages the server can receive from the
// client.
typedef enum {
  LIST_USERS = 1,
  GET_USER,
  CHANGE_STATUS,
  SEND_MESSAGE,
  GET_MESSAGES,
} UWU_ServerMessages;

// Represents all the "type codes" of messages the client receive from the
// server.
typedef enum {
  ERROR = 50,
  LISTED_USERS,
  GOT_USER,
  REGISTERED_USER,
  CHANGED_STATUS,
  GOT_MESSAGE,
  GOT_MESSAGES,
} UWU_ClientMessages;

typedef enum {
  // The user you tried to access doesn't exist!
  USER_NOT_FOUND,
  // The status you want to change to doesn't exist!
  INVALID_STATUS,
  // The message you wish to send is empty!
  EMPTY_MESSAGE,
  // You're trying to communicate with a disconnected user!
  USER_ALREADY_DISCONNECTED,
} UWU_Errors;

/* *****************************************************************************
Arenas
***************************************************************************** */

// The arena is a simple abstraction over the concept of storing data on the
// heap. Think of it as a fixed append-only stack, you can only append elements
// to the end or remove them all together.
struct UWU_Arena {
  size_t capacity;
  size_t size;
  uint8_t *data;
};

static UWU_ERR UWU_MALLOC_ERROR = (UWU_ERR)1;

// Initializes a new arena with the specified capacity!
struct UWU_Arena UWU_Arena_init(size_t capacity, UWU_ERR err) {
  void *data = malloc(sizeof(uint8_t) * capacity);

  struct UWU_Arena arena = {};

  if (data == NULL) {
    err = UWU_MALLOC_ERROR;
    return arena;
  }

  arena.capacity = capacity;
  arena.size = 0;
  arena.data = data;

  return arena;
}

static UWU_ERR UWU_ARENA_FAILED_ALLOCATION = (UWU_ERR)1;
// Tries to allocate on the arena.
//
// - arena: The specific arena to use for allocation.
// - size: The amount of bytes to allocate.
// - err: The err parameter, refer to the start of lib for an explanation.
//
// Success: Returns a pointer to the first byte of the memory region requested.
// Failure: Sets err equal to `UWU_ARENA_FAILED_ALLOCATION`.
void *UWU_Arena_alloc(struct UWU_Arena *arena, size_t size, size_t *err) {
  bool has_space = arena->size + size < arena->capacity;
  if (!has_space) {
    err = UWU_ARENA_FAILED_ALLOCATION;
    return NULL;
  } else {
    uint8_t *mem_start = &arena->data[arena->size];
    arena->size += size;
    return mem_start;
  }
}

// Resets the arena for future use.
// IT DOES NOT FREE THE MEMORY OF THE ARENA! Use `deinit` for that.
void UWU_Arena_reset(struct UWU_Arena *arena) { arena->size = 0; }

// Resets and frees all memory associated with this arena.
// DO NOT USE an arena that has already been deinited!
void UWU_Arena_deinit(struct UWU_Arena *arena) {
  arena->capacity = 0;
  arena->size = 0;
  free(arena->data);
}

/* *****************************************************************************
Strings
***************************************************************************** */

// Represents a string "slice"
//
// Only the creator of the original slice needs to free this memory!
struct UWU_String {
  // Contains all the data of this string
  // CAREFUL: It may or may not be a "null terminated string"!
  uint8_t *data;
  // The length of data that is considered to be "this string"
  size_t length;
};

uint8_t UWU_String_getChar(struct UWU_String *str, size_t idx) {
  if (idx >= 0 && idx < str->length) {
    return str->data[idx];
  }

  UWU_PANIC("Out of bound access on String `%s` with Idx `%d`", str, idx);
  return 0;
}

// Checks if the given
bool UWU_String_equal(struct UWU_String *a, struct UWU_String *b) {
  if (a->length != b->length) {
    return FALSE;
  }

  for (size_t i = 0; i < a->length; i++) {
    if (a->data[i] != b->data[i]) {
      return FALSE;
    }
  }

  return TRUE;
}

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
