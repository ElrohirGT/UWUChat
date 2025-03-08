#include "fiobject.h"
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
typedef struct {
  size_t capacity;
  size_t size;
  uint8_t *data;
} UWU_Arena;

static UWU_ERR UWU_ERR_MALLOC_ERROR = (UWU_ERR)1;

// Initializes a new arena with the specified capacity!
UWU_Arena UWU_Arena_init(size_t capacity, UWU_ERR err) {
  UWU_Arena arena = {};
  arena.data = malloc(sizeof(uint8_t) * capacity);

  if (arena.data == NULL) {
    err = UWU_ERR_MALLOC_ERROR;
    return arena;
  }

  arena.capacity = capacity;
  arena.size = 0;

  return arena;
}

// {
// let arena = malloc()
//
// while (true) {
//
// let a = arena.alloc(adlfkjlasdf)
// let b = arena.alloc(asdkfjalskdjf)
//
// arena.reset()
// }
//
// free(arena)
// }

static UWU_ERR UWU_ERR_ARENA_FAILED_ALLOCATION = (UWU_ERR)1;
// Tries to allocate on the arena.
//
// - arena: The specific arena to use for allocation.
// - size: The amount of bytes to allocate.
// - err: The err parameter, refer to the start of lib for an explanation.
//
// Success: Returns a pointer to the first byte of the memory region requested.
// Failure: Sets err equal to `UWU_ARENA_FAILED_ALLOCATION`.
void *UWU_Arena_alloc(UWU_Arena *arena, size_t size, size_t *err) {
  bool has_space = arena->size + size <= arena->capacity;
  if (!has_space) {
    err = UWU_ERR_ARENA_FAILED_ALLOCATION;
    return NULL;
  } else {
    void *mem_start = &arena->data[arena->size];
    arena->size += size;
    return mem_start;
  }
}

// Resets the arena for future use.
// IT DOES NOT FREE THE MEMORY OF THE ARENA! Use `deinit` for that.
void UWU_Arena_reset(UWU_Arena *arena) { arena->size = 0; }

// Resets and frees all memory associated with this arena.
// DO NOT USE an arena that has already been deinited!
void UWU_Arena_deinit(UWU_Arena *arena) {
  arena->capacity = 0;
  arena->size = 0;
  free(arena->data);
}

/* *****************************************************************************
Strings
***************************************************************************** */

// SoyUnaCadena\0
// Soy
// UnaCadeasdlfkjliqjwelkjasdf\0

// Represents a string "slice"
//
// Only the creator of the original slice needs to free this memory!
typedef struct {
  // Contains all the data of this string
  //
  // CAREFUL: It may or may not be a "null terminated string"!
  char *data;
  // The length of data that is considered to be "this string"
  size_t length;
} UWU_String;

// Converts from a `UWU_String` to a null terminated string.
char *UWU_String_toCstr(UWU_String *str, UWU_ERR err) {
  char *c_str = malloc(str->length + 1);

  if (c_str == NULL) {
    err = UWU_ERR_MALLOC_ERROR;
    return c_str;
  }

  for (size_t i = 0; i < str->length + 1; i++) {
    c_str[i] = str->data[i];
  }
  c_str[str->length] = 0;

  return c_str;
}

// Copies a fiobj into a `UWU_String`.
//
// `obj` must be a `FIOBJ_T_STRING`, if not this function panics!
//
// To free this data please see `UWU_String_free`.
UWU_String *UWU_String_copyFromFio(FIOBJ obj, UWU_ERR err) {
  if (!FIOBJ_TYPE_IS(obj, FIOBJ_T_STRING)) {
    UWU_PANIC("Trying to copy from a Fio object that is not a string!");
    return NULL;
  }

  fio_str_info_s c_str = fiobj_obj2cstr(obj);
  char *data = malloc(c_str.len);
  for (size_t i = 0; i < c_str.len; i++) {
    data[i] = c_str.data[i];
  }

  UWU_String *str = malloc(sizeof(UWU_String));

  if (str == NULL) {
    err = UWU_ERR_MALLOC_ERROR;
    return NULL;
  }

  str->data = data;
  str->length = c_str.len;

  return str;
}

// Creates a new `UWU_String`.
//
// This method should only be used if you know that the returned `UWU_String`
// will live for as long as `char*` data will.
UWU_String UWU_String_new(char *data, size_t length) {
  UWU_String str = {.data = data, .length = length};
  return str;
}

uint8_t UWU_String_getChar(UWU_String *str, size_t idx) {
  if (idx >= 0 && idx < str->length) {
    return str->data[idx];
  }

  UWU_ERR err = NO_ERROR;
  char *c_str = UWU_String_toCstr(str, err);

  if (err != NO_ERROR) {
    UWU_PANIC("Can't convert UWU_String into C_str! (len: %d, data: %s)",
              str->length, str->data);
    return 0;
  }

  UWU_PANIC("Out of bound access on String `%s` with Idx `%d`", c_str, idx);
  free(c_str);

  return 0;
}

// Checks if the given `a` string is equal to the other `b` string.
bool UWU_String_equal(UWU_String *a, UWU_String *b) {
  if (a->length != b->length) {
    return FALSE;
  }

  for (size_t i = 0; i < a->length; i++) {
    if (UWU_String_getChar(a, i) != UWU_String_getChar(b, i)) {
      return FALSE;
    }
  }

  return TRUE;
}

// Represents a message on a given chat history.
//
// The ChatEntry should own it's memory! So it should receive a copy of
// `content` and `origin_username`.
typedef struct {
  // The content of the message.
  UWU_String content;
  // The username of the person sending the message.
  UWU_String origin_username;
} UWU_ChatEntry;

// Represents a message history of a certain chat
typedef struct {
  // A pointer to an array of `ChatEntry`.
  UWU_ChatEntry *messages;
  // The number of chat messages filling the array.
  uint32_t count;
  // How much memory is left in the array of `ChatEntry`.
  uint32_t capacity;
} UWU_History;
