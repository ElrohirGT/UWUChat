/**
In this a Hello World example using the bundled HTTP / WebSockets extension.

Compile using:

    NAME=http make

Or test the `poll` engine's performance by compiling with `poll`:

    FIO_POLL=1 NAME=http make

Run with:

    ./tmp/http -t 1

Benchmark with keep-alive:

    ab -c 200 -t 4 -n 1000000 -k http://127.0.0.1:3000/
    wrk -c200 -d4 -t1 http://localhost:3000/

Benchmark with higher load:

    ab -c 4400 -t 4 -n 1000000 -k http://127.0.0.1:3000/
    wrk -c4400 -d4 -t1 http://localhost:3000/

Use a javascript console to connect to the WebSocket chat service... maybe using
the following javascript code:

    // run 1st client app on port 3000.
    ws = new WebSocket("ws://localhost:3000/Mitchel");
    ws.onmessage = function(e) { console.log(e.data); };
    ws.onclose = function(e) { console.log("closed"); };
    ws.onopen = function(e) { e.target.send("Yo!"); };

    // run 2nd client app on port 3030, to test Redis
    ws = new WebSocket("ws://localhost:3030/Johana");
    ws.onmessage = function(e) { console.log(e.data); };
    ws.onclose = function(e) { console.log("closed"); };
    ws.onopen = function(e) { e.target.send("Brut."); };

It's possible to use SSE (Server-Sent-Events / EventSource) for listening in on
the chat:

    var source = new EventSource("/Watcher");
    source.addEventListener('message', (e) => { console.log(e.data); });
    source.addEventListener('open', (e) => {
      console.log("SSE Connection open.");
    }); source.addEventListener('close', (e) => {
      console.log("SSE Connection lost."); });

Remember that published messages will now be printed to the console both by
Mitchel and Johana, which means messages will be delivered twice unless using
two different browser windows.
*/

#include "hashmap.h"
#include "lib.c"

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

/* *****************************************************************************
Constants
***************************************************************************** */

// The separator used for separating usernames in the hashmap.
// A username should not include this sequence of characters.
static UWU_String SEPARATOR = {.data = "&/)", .length = strlen("&/)")};

/* *****************************************************************************
Utilities functions
***************************************************************************** */

int remove_if_matches(void *context, struct hashmap_element_s *const e) {
  UWU_String *user_name = context;
  UWU_String hash_key = {
      .data = (char *)e->key,
      .length = e->key_len,
  };

  UWU_String tmp_after = UWU_String_combineWithOther(user_name, &SEPARATOR);
  UWU_String tmp_before = UWU_String_combineWithOther(&SEPARATOR, user_name);

  bool starts_with_username = UWU_String_startsWith(&hash_key, &tmp_after);
  bool ends_with_username = UWU_String_endsWith(&hash_key, &tmp_before);

  UWU_String_freeWithMalloc(&tmp_after);
  UWU_String_freeWithMalloc(&tmp_before);

  if (starts_with_username || ends_with_username) {
    UWU_ChatHistory *data = e->data;

    UWU_ChatHistory_deinit(data);
    free(data);
    return -1;
  }

  return 0;
}

/* *****************************************************************************
Server State
***************************************************************************** */

// Collection that saves all the chat histories from all usernames.
typedef struct {
  UWU_ChatHistory *data;
  uint8_t length;
  uint8_t capacity;
} UWU_ChatHistoryCollection;

// Global group chat
static fio_str_info_s GROUP_CHAT_CHANNEL = {.data = "~", .len = strlen("~")};

// Si tenemos "n" usuarios conectados entonces tendremos una cantidad de chats
// igual a:
//
//    n!			(n-1)n
// -------- = ------		(siempre que n >= 2)
// 2!(n-2)!     2
//
// Esta función tiene crecimiento cuadrático, por lo que es importante hacer que
// el acceso al historial de mensajes de cada chat no crezca en complejidad a la
// misma velocidad. Por esto es que vamos a usar un hashmap:
//
// * Las llaves serán la combinación de los dos usernames ordenados
// alfabéticamente.
// * Los valores serán los historiales de mensajes.

// The max quantity of users that can be active..
// const size_t MAX_ACTIVE_USERS = 255;

// The max quantity of messages a chat history can hold...
const size_t MAX_MESSAGES_PER_CHAT = 100;

// Saves all the active usernames...
static UWU_UserList active_usernames;
// Saves all the chat active chat histories...
// Key: The combination of both usernames as a UWU_String.
// Value: An UWU_History item.
static struct hashmap_s chats;
// Saves all the chat history messages from the Group chat
static UWU_ChatHistory group_chat;

// Initializes the server state...
void initialize_server_state(UWU_ERR err) {
  active_usernames = UWU_UserList_init();

  char *group_chat_name = malloc(sizeof(char));
  if (group_chat_name == NULL) {
    err = MALLOC_FAILED;
    return;
  }
  *group_chat_name = '~';
  UWU_String uwu_name = {.data = group_chat_name, .length = 1};

  group_chat = UWU_ChatHistory_init(MAX_MESSAGES_PER_CHAT * 3, uwu_name, err);
  if (err != NO_ERROR) {
    return;
  }

  if (0 != hashmap_create(8, &chats)) {
    err = HASHMAP_INITIALIZATION_ERROR;
    return;
  }

  // TODO: Initialize other server state...
}

void deinitialize_server_state() {
  fprintf(stderr, "Cleaning User List...\n");
  UWU_UserList_deinit(&active_usernames);
  fprintf(stderr, "Cleaning group Chat history...\n");
  UWU_ChatHistory_deinit(&group_chat);
  fprintf(stderr, "Cleaning DM Chat histories...\n");
  hashmap_destroy(&chats);
}

/* *****************************************************************************
The main function
*****************************************************************************
*/

/* HTTP request handler */
static void on_http_request(http_s *h);
/* HTTP upgrade request handler */
static void on_http_upgrade(http_s *h, char *requested_protocol, size_t len);
/* Command Line Arguments Management */
static void initialize_cli(int argc, char const *argv[]);
/* Initializes Redis, if set by command line arguments */
static void initialize_redis(void);

int main(int argc, char const *argv[]) {
  initialize_cli(argc, argv);
  initialize_redis();
  /* TLS support */
  fio_tls_s *tls = NULL;
  if (fio_cli_get_bool("-tls")) {
    char local_addr[1024];
    local_addr[fio_local_addr(local_addr, 1023)] = 0;
    tls = fio_tls_new(local_addr, NULL, NULL, NULL);
  }
  /* optimize WebSocket pub/sub for multi-connection broadcasting */
  websocket_optimize4broadcasts(WEBSOCKET_OPTIMIZE_PUBSUB, 1);

  UWU_ERR err = NO_ERROR;

  initialize_server_state(err);

  if (err != NO_ERROR) {
    fprintf(stderr,
            "An error during the initialization of the server state!\n");

    fio_cli_end();
    fio_tls_destroy(tls);
    return 1;
  }

  /* listen for incoming connections */
  const char *port = fio_cli_get("-p");
  const char *host = fio_cli_get("-b");
  if (http_listen(port, host, .on_request = on_http_request,
                  .on_upgrade = on_http_upgrade,
                  .max_body_size = (fio_cli_get_i("-maxbd") * 1024 * 1024),
                  .ws_max_msg_size = (fio_cli_get_i("-maxms") * 1024),
                  .public_folder = fio_cli_get("-public"),
                  .log = fio_cli_get_bool("-log"),
                  .timeout = fio_cli_get_i("-keep-alive"), .tls = tls,
                  .ws_timeout = fio_cli_get_i("-ping")) == -1) {
    /* listen failed ?*/
    deinitialize_server_state();
    perror(
        "ERROR: facil.io couldn't initialize HTTP service (already running?)");
    exit(1);
  }

  fprintf(stderr, "Listening on %s:%s...\n", host, port);
  fio_start(.threads = fio_cli_get_i("-t"), .workers = fio_cli_get_i("-w"));

  // Cleaning up...
  fprintf(stderr, "Shutting down server...\n");
  deinitialize_server_state();
  fio_cli_end();
  fio_tls_destroy(tls);
  return 0;
}

/* *****************************************************************************
HTTP Request / Response Handling
***************************************************************************** */

static void on_http_request(http_s *h) {
  /* set a response and send it (finnish vs. destroy). */
  http_send_body(h, "<The HTTP response is useless>", 30);
}

/* *****************************************************************************
HTTP Upgrade Handling
***************************************************************************** */

/* Server Sent Event Handlers */
static void sse_on_open(http_sse_s *sse);
static void sse_on_close(http_sse_s *sse);

/* WebSocket Handlers */
static void ws_on_open(ws_s *ws);
static void ws_on_message(ws_s *ws, fio_str_info_s msg, uint8_t is_text);
static void ws_on_shutdown(ws_s *ws);
static void ws_on_close(intptr_t uuid, void *udata);

/* HTTP upgrade callback */
static void on_http_upgrade(http_s *h, char *requested_protocol, size_t len) {

  fprintf(stderr, "Received a connection request with a query parameter: %s\n",
          fiobj_obj2cstr(h->query).data);

  // First we parse the body of the request,
  // this must be done before parsing the query params.
  http_parse_body(h);
  http_parse_query(h);

  if (FIOBJ_TYPE_IS(h->params, FIOBJ_T_NULL)) {
    fprintf(stderr, "400 - NO QUERY PARAMETERS SUPPLIED!\n");
    http_send_error(h, 400);
    return;
  }

  // Since we already parsed the request we should be able to access data on the
  // params hash
  const FIOBJ key = fiobj_str_new("name", 4);
  FIOBJ fio_nickname = fiobj_hash_get(h->params, key);

  if (fio_nickname == FIOBJ_INVALID) {
    fprintf(stderr, "400 - NO USERNAME SUPPLIED!\n");
    http_send_error(h, 400);
    return;
  }

  fio_str_info_s c_nickname = fiobj_obj2cstr(fio_nickname);
  int is_group_chat = strcmp(c_nickname.data, "~") == 0;
  int is_too_large = c_nickname.len > 255;

  if (is_too_large) {
    fprintf(stderr, "Username too large! (length: %zu)\n", c_nickname.len);
  }

  if (is_group_chat || is_too_large) {
    fprintf(stderr, "400 - INVALID USERNAME SUPPLIED!\n");
    http_send_error(h, 400);
    return;
  }

  fprintf(stderr, "The nickname `%s` is valid! Connecting...\n",
          c_nickname.data);

  UWU_ERR err = NO_ERROR;
  UWU_String *uwu_nickname = malloc(sizeof(UWU_String));
  *uwu_nickname = UWU_String_copyFromFio(fio_nickname, err);

  if (err != NO_ERROR) {
    fprintf(stderr, "ERROR: Can't copy username from Facil.io into local "
                    "representation!\n");
    http_send_error(h, 500);
  }

  /* Test for upgrade protocol (websocket vs. sse) */
  if (len == 3 && requested_protocol[1] == 's') {
    if (fio_cli_get_bool("-v")) {
      fprintf(stderr, "* (%d) new SSE connection: %s.\n", getpid(),
              c_nickname.data);
    }
    http_upgrade2sse(h, .on_open = sse_on_open, .on_close = sse_on_close,
                     .udata = (void *)fio_nickname);
  } else if (len == 9 && requested_protocol[1] == 'e') {
    if (fio_cli_get_bool("-v")) {
      fprintf(stderr, "* (%d) new WebSocket connection: %s.\n", getpid(),
              c_nickname.data);
    }
    http_upgrade2ws(h, .on_message = ws_on_message, .on_open = ws_on_open,
                    .on_shutdown = ws_on_shutdown, .on_close = ws_on_close,
                    .udata = uwu_nickname);
  } else {
    fprintf(stderr, "WARNING: unrecognized HTTP upgrade request: %s\n",
            requested_protocol);
    http_send_error(h, 400);
    // fiobj_free(fio_nick_copy); // we didn't use this
  }
}

/* *****************************************************************************
HTTP SSE (Server Sent Events) Callbacks
***************************************************************************** */

/**
 * The (optional) on_open callback will be called once the EventSource
 * connection is established.
 */
static void sse_on_open(http_sse_s *sse) {
  http_sse_write(sse, .data = {.data = "Welcome to the SSE chat channel.\r\n"
                                       "You can only listen, not write.",
                               .len = 65});
  http_sse_subscribe(sse, .channel = GROUP_CHAT_CHANNEL);
  http_sse_set_timout(sse, fio_cli_get_i("-ping"));
  FIOBJ tmp = fiobj_str_copy((FIOBJ)sse->udata);
  fiobj_str_write(tmp, " joind the chat only to listen.", 31);
  fio_publish(.channel = GROUP_CHAT_CHANNEL, .message = fiobj_obj2cstr(tmp));
  fiobj_free(tmp);
}

static void sse_on_close(http_sse_s *sse) {
  /* Let everyone know we left the chat */
  fiobj_str_write((FIOBJ)sse->udata, " left the chat.", 15);
  fio_publish(.channel = GROUP_CHAT_CHANNEL,
              .message = fiobj_obj2cstr((FIOBJ)sse->udata));
  /* free the nickname */
  fiobj_free((FIOBJ)sse->udata);
}

/* *****************************************************************************
WebSockets Callbacks
***************************************************************************** */

static void ws_on_message(ws_s *ws, fio_str_info_s msg, uint8_t is_text) {
  // Add the Nickname to the message
  FIOBJ str = fiobj_str_copy((FIOBJ)websocket_udata_get(ws));
  fiobj_str_write(str, ": ", 2);
  fiobj_str_write(str, msg.data, msg.len);
  // publish
  fio_publish(.channel = GROUP_CHAT_CHANNEL, .message = fiobj_obj2cstr(str));
  // free the string
  fiobj_free(str);
  (void)is_text; // we don't care.
  (void)ws;      // this could be used to send an ACK, but we don't.
}

// When a new user connects to the server we need to do a lot of stuff:
// - Add the user as an active user.
// - Initialize all it's state.
// - Subscribe to the group chat and other chats.
// - Notify other clients that this user has recently connected.
static void ws_on_open(ws_s *ws) {
  // websocket_write(
  //     ws, (fio_str_info_s){.data = "Welcome to the chat-room.", .len = 25},
  //     1);

  UWU_ERR err = NO_ERROR;

  // 1. Add the user as an active user.
  UWU_String *user_name = websocket_udata_get(ws);
  if (err != NO_ERROR) {
    char *c_str = UWU_String_toCStr(user_name);
    UWU_PANIC("Failed to add username `%s` to the UserCollection!", c_str);
    return;
  }

  UWU_User user = {.username = *user_name, .status = ACTIVE};

  struct UWU_UserListNode node = UWU_UserListNode_newWithValue(user);
  UWU_UserList_insertEnd(&active_usernames, &node, err);
  if (err != NO_ERROR) {
    char *c_str = UWU_String_toCStr(user_name);
    UWU_PANIC("Failed to add username `%s` to the UserCollection!", c_str);
    return;
  }

  for (struct UWU_UserListNode *current = active_usernames.start;
       current != NULL; current = current->next) {

    if (current->is_sentinel) {
      continue;
    }

    UWU_String current_username = current->data.username;
    UWU_String *first = &current_username;
    UWU_String *other = user_name;

    if (!UWU_String_firstGoesFirst(first, other)) {
      first = user_name;
      other = &current_username;
    }

    UWU_String tmp = UWU_String_combineWithOther(first, &SEPARATOR);
    UWU_String combined = UWU_String_combineWithOther(&tmp, other);
    UWU_String_freeWithMalloc(&tmp);

    // Subscribe to all DM channels...
    fio_str_info_s channel = {.data = combined.data, .len = combined.length};
    websocket_subscribe(ws, .channel = channel);

    UWU_ChatHistory *ht = malloc(sizeof(UWU_ChatHistory));
    *ht = UWU_ChatHistory_init(MAX_MESSAGES_PER_CHAT, combined, err);
    hashmap_put(&chats, combined.data, combined.length, ht);
  }

  // Subscribe to group channel
  websocket_subscribe(ws, .channel = GROUP_CHAT_CHANNEL);

  // FIXME: Change message!
  // 3. Notify other clients that this user has recently connected...
  FIOBJ tmp = fiobj_str_new(user_name->data, user_name->length);
  fiobj_str_write(tmp, " joind the chat.", 16);
  fio_publish(.channel = GROUP_CHAT_CHANNEL, .message = fiobj_obj2cstr(tmp));
  fiobj_free(tmp);
}

static void ws_on_shutdown(ws_s *ws) {

  websocket_write(
      ws, (fio_str_info_s){.data = "Server shutting down, goodbye.", .len = 30},
      1);
}

static void ws_on_close(intptr_t uuid, void *udata) {
  UWU_String *user_name = udata;

  hashmap_iterate_pairs(&chats, remove_if_matches, user_name);

  UWU_UserList_removeByUsernameIfExists(&active_usernames, user_name);

  // Now we need to free the UWU_String!
  UWU_String_freeWithMalloc(user_name);
  free(user_name);

  /* Let everyone know we left the chat */
  // fiobj_str_write((FIOBJ)udata, " left the chat.", 15);
  // fio_publish(.channel = GROUP_CHAT_CHANNEL,
  //             .message = fiobj_obj2cstr((FIOBJ)udata));
  // /* free the nickname */
  // fiobj_free((FIOBJ)udata);
  // (void)uuid; // we don't use the ID
}

/* *****************************************************************************
Redis initialization
***************************************************************************** */
static void initialize_redis(void) {
  if (!fio_cli_get("-redis") || !strlen(fio_cli_get("-redis")))
    return;
  FIO_LOG_STATE("* Initializing Redis connection to %s\n",
                fio_cli_get("-redis"));
  fio_url_s info =
      fio_url_parse(fio_cli_get("-redis"), strlen(fio_cli_get("-redis")));
  fio_pubsub_engine_s *e =
      redis_engine_create(.address = info.host, .port = info.port,
                          .auth = info.password);
  if (e)
    fio_state_callback_add(FIO_CALL_ON_FINISH,
                           (void (*)(void *))redis_engine_destroy, e);
  FIO_PUBSUB_DEFAULT = e;
}

/* *****************************************************************************
CLI helpers
***************************************************************************** */
static void initialize_cli(int argc, char const *argv[]) {
  /*     ****  Command line arguments ****     */
  fio_cli_start(
      argc, argv, 0, 0, NULL,
      // Address Binding
      FIO_CLI_PRINT_HEADER("Address Binding:"),
      FIO_CLI_INT("-port -p port number to listen to. defaults port 3000"),
      "-bind -b address to listen to. defaults any available.",
      FIO_CLI_BOOL("-tls use a self signed certificate for TLS."),
      // Concurrency
      FIO_CLI_PRINT_HEADER("Concurrency:"),
      FIO_CLI_INT("-workers -w number of processes to use."),
      FIO_CLI_INT("-threads -t number of threads per process."),
      // HTTP Settings
      FIO_CLI_PRINT_HEADER("HTTP Settings:"),
      "-public -www public folder, for static file service.",
      FIO_CLI_INT(
          "-keep-alive -k HTTP keep-alive timeout (0..255). default: 10s"),
      FIO_CLI_INT(
          "-max-body -maxbd HTTP upload limit in Mega Bytes. default: 50Mb"),
      FIO_CLI_BOOL("-log -v request verbosity (logging)."),
      // WebSocket Settings
      FIO_CLI_PRINT_HEADER("WebSocket Settings:"),
      FIO_CLI_INT("-ping websocket ping interval (0..255). default: 40s"),
      FIO_CLI_INT("-max-msg -maxms incoming websocket message "
                  "size limit in Kb. default: 250Kb"),
      // Misc Settings
      FIO_CLI_PRINT_HEADER("Misc:"),
      FIO_CLI_STRING("-redis -r an optional Redis URL server address."),
      FIO_CLI_PRINT("\t\ta valid Redis URL would follow the pattern:"),
      FIO_CLI_PRINT("\t\t\tredis://user:password@localhost:6379/"),
      FIO_CLI_INT("-verbosity -V facil.io verbocity 0..5 (logging level)."));

  /* Test and set any default options */
  if (!fio_cli_get("-p")) {
    /* Test environment as well */
    char *tmp = getenv("PORT");
    if (!tmp)
      tmp = "3000";
    /* CLI et functions (unlike fio_cli_start) ignores aliases */
    fio_cli_set("-p", tmp);
    fio_cli_set("-port", tmp);
  }
  if (!fio_cli_get("-b")) {
    char *tmp = getenv("ADDRESS");
    if (tmp) {
      fio_cli_set("-b", tmp);
      fio_cli_set("-bind", tmp);
    }
  }
  if (!fio_cli_get("-public")) {
    char *tmp = getenv("HTTP_PUBLIC_FOLDER");
    if (tmp) {
      fio_cli_set("-public", tmp);
      fio_cli_set("-www", tmp);
    }
  }

  if (!fio_cli_get("-redis")) {
    char *tmp = getenv("REDIS_URL");
    if (tmp) {
      fio_cli_set("-redis", tmp);
      fio_cli_set("-r", tmp);
    }
  }
  if (fio_cli_get("-V")) {
    FIO_LOG_LEVEL = fio_cli_get_i("-V");
  }

  fio_cli_set_default("-ping", "40");

  /* CLI set functions (unlike fio_cli_start) ignores aliases */
  fio_cli_set_default("-k", "10");
  fio_cli_set_default("-keep-alive", "10");

  fio_cli_set_default("-max-body", "50");
  fio_cli_set_default("-maxbd", "50");

  fio_cli_set_default("-max-message", "250");
  fio_cli_set_default("-maxms", "250");
}
