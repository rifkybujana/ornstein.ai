#include "llm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include <curl/curl.h>

/* ── Token queue ──────────────────────────────────────────────────── */

#define TOKEN_QUEUE_SIZE 256
#define TOKEN_MAX_LEN    64

typedef struct {
    char text[TOKEN_MAX_LEN];
    int  done;
} TokenEntry;

static TokenEntry       token_queue[TOKEN_QUEUE_SIZE];
static int              queue_head, queue_tail;
static pthread_mutex_t  queue_mutex = PTHREAD_MUTEX_INITIALIZER;

static void queue_push(const char *text, int done) {
    pthread_mutex_lock(&queue_mutex);
    int next = (queue_tail + 1) % TOKEN_QUEUE_SIZE;
    if (next != queue_head) {                       /* drop if full */
        strncpy(token_queue[queue_tail].text, text, TOKEN_MAX_LEN - 1);
        token_queue[queue_tail].text[TOKEN_MAX_LEN - 1] = '\0';
        token_queue[queue_tail].done = done;
        queue_tail = next;
    }
    pthread_mutex_unlock(&queue_mutex);
}

static int queue_pop(TokenEntry *out) {
    pthread_mutex_lock(&queue_mutex);
    if (queue_head == queue_tail) {
        pthread_mutex_unlock(&queue_mutex);
        return 0;
    }
    *out = token_queue[queue_head];
    queue_head = (queue_head + 1) % TOKEN_QUEUE_SIZE;
    pthread_mutex_unlock(&queue_mutex);
    return 1;
}

/* ── Server process ───────────────────────────────────────────────── */

static pid_t server_pid = -1;
static int   server_is_ready = 0;

static char  saved_model_path[1024];
static char  saved_server_path[1024];

/* ── Inference thread state ───────────────────────────────────────── */

static pthread_t    infer_thread;
static int          infer_thread_running = 0;
static int          infer_cancel = 0;

static llm_token_cb current_cb = NULL;
static void        *current_userdata = NULL;

/* Protected copy of the request for the background thread */
static char request_body[2048];

/* ── curl write callback for SSE streaming ────────────────────────── */

/* Extract the value of "content" from a JSON fragment.
   Looks for "content":" and extracts the string value. */
static int extract_content(const char *json, char *out, int out_size) {
    const char *key = "\"content\":\"";
    const char *p = strstr(json, key);
    if (!p) {
        /* Also try with a space after colon: "content": " */
        key = "\"content\": \"";
        p = strstr(json, key);
        if (!p) return 0;
    }
    p += strlen(key);

    int i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;  /* skip backslash */
            switch (*p) {
                case 'n':  out[i++] = '\n'; break;
                case 't':  out[i++] = '\t'; break;
                case '"':  out[i++] = '"';  break;
                case '\\': out[i++] = '\\'; break;
                default:   out[i++] = *p;   break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return i > 0;
}

/* Buffer for accumulating partial SSE lines across curl callbacks */
typedef struct {
    char buf[4096];
    int  len;
} SSEBuffer;

static size_t sse_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    SSEBuffer *sse = (SSEBuffer *)userdata;

    for (size_t i = 0; i < total; i++) {
        if (infer_cancel) return 0;   /* abort transfer */

        char c = ptr[i];
        if (c == '\n') {
            sse->buf[sse->len] = '\0';

            /* Process complete line */
            if (strncmp(sse->buf, "data: [DONE]", 12) == 0) {
                queue_push("", 1);
            } else if (strncmp(sse->buf, "data: ", 6) == 0) {
                char content[TOKEN_MAX_LEN];
                if (extract_content(sse->buf + 6, content, TOKEN_MAX_LEN)) {
                    queue_push(content, 0);
                }
            }
            sse->len = 0;
        } else if (sse->len < (int)sizeof(sse->buf) - 1) {
            sse->buf[sse->len++] = c;
        }
    }
    return total;
}

/* ── Inference thread ─────────────────────────────────────────────── */

static void *infer_thread_fn(void *arg) {
    (void)arg;

    CURL *curl = curl_easy_init();
    if (!curl) {
        queue_push("[error: curl init failed]", 1);
        return NULL;
    }

    SSEBuffer sse = { .len = 0 };

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL,
                     "http://localhost:8231/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sse_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sse);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK && !infer_cancel) {
        char errmsg[128];
        snprintf(errmsg, sizeof(errmsg), "[error: %s]",
                 curl_easy_strerror(res));
        queue_push(errmsg, 1);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    infer_thread_running = 0;
    return NULL;
}

/* ── Build JSON request body ──────────────────────────────────────── */

/* Escape a string for JSON embedding (minimal: handle \, ", newline, tab) */
static void json_escape(const char *src, char *dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 2; i++) {
        switch (src[i]) {
            case '"':  dst[j++] = '\\'; dst[j++] = '"';  break;
            case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
            case '\n': dst[j++] = '\\'; dst[j++] = 'n';  break;
            case '\t': dst[j++] = '\\'; dst[j++] = 't';  break;
            default:   dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
}

static void build_request(const char *mood, const char *user_msg) {
    char system_prompt[512];
    snprintf(system_prompt, sizeof(system_prompt),
             "You are a cute desktop pet named Ornstein. "
             "You speak in short, expressive sentences. "
             "You're currently feeling %s. "
             "Keep responses under 2 sentences.", mood);

    char escaped_system[512];
    char escaped_user[512];
    json_escape(system_prompt, escaped_system, sizeof(escaped_system));
    json_escape(user_msg, escaped_user, sizeof(escaped_user));

    snprintf(request_body, sizeof(request_body),
             "{"
             "\"messages\":["
             "{\"role\":\"system\",\"content\":\"%s\"},"
             "{\"role\":\"user\",\"content\":\"%s\"}"
             "],"
             "\"stream\":true"
             "}",
             escaped_system, escaped_user);
}

/* ── Public API ───────────────────────────────────────────────────── */

int llm_init(const char *model_path, const char *server_path) {
    /* Verify model file exists */
    if (access(model_path, F_OK) != 0) {
        fprintf(stderr, "llm_init: model file not found: %s\n", model_path);
        return -1;
    }

    strncpy(saved_model_path, model_path, sizeof(saved_model_path) - 1);
    saved_model_path[sizeof(saved_model_path) - 1] = '\0';
    strncpy(saved_server_path, server_path, sizeof(saved_server_path) - 1);
    saved_server_path[sizeof(saved_server_path) - 1] = '\0';

    curl_global_init(CURL_GLOBAL_DEFAULT);

    /* Fork and exec llama-server */
    pid_t pid = fork();
    if (pid < 0) {
        perror("llm_init: fork");
        return -1;
    }

    if (pid == 0) {
        /* Child process — redirect stdout/stderr to /dev/null */
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        execl(server_path, "llama-server",
              "-m", model_path,
              "--port", "8231",
              "-ngl", "99",
              "-c", "2048",
              "--log-disable",
              (char *)NULL);

        /* execl only returns on error */
        _exit(127);
    }

    server_pid = pid;
    printf("llm_init: spawned llama-server (pid %d)\n", pid);

    /* Poll health endpoint until ready (max 30s, 500ms intervals) */
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "llm_init: curl_easy_init failed\n");
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
        server_pid = -1;
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8231/health");
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);    /* HEAD-like: no body */
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    int ready = 0;
    for (int attempt = 0; attempt < 60; attempt++) {   /* 60 * 500ms = 30s */
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code == 200) {
                ready = 1;
                break;
            }
        }
        usleep(500000);   /* 500ms */

        /* Check if child has died */
        int status;
        pid_t w = waitpid(server_pid, &status, WNOHANG);
        if (w > 0) {
            fprintf(stderr, "llm_init: llama-server exited prematurely "
                    "(status %d)\n", WEXITSTATUS(status));
            curl_easy_cleanup(curl);
            server_pid = -1;
            return -1;
        }
    }

    curl_easy_cleanup(curl);

    if (!ready) {
        fprintf(stderr, "llm_init: llama-server health check timed out\n");
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
        server_pid = -1;
        return -1;
    }

    server_is_ready = 1;
    printf("llm_init: llama-server is ready\n");
    return 0;
}

void llm_send(const char *mood, const char *user_msg,
              llm_token_cb cb, void *userdata) {
    if (!server_is_ready) return;

    /* If a previous inference is running, cancel it and wait */
    if (infer_thread_running) {
        infer_cancel = 1;
        pthread_join(infer_thread, NULL);
        infer_thread_running = 0;
        infer_cancel = 0;

        /* Drain leftover tokens from the cancelled request */
        pthread_mutex_lock(&queue_mutex);
        queue_head = queue_tail = 0;
        pthread_mutex_unlock(&queue_mutex);
    }

    current_cb = cb;
    current_userdata = userdata;

    build_request(mood, user_msg);

    infer_cancel = 0;
    infer_thread_running = 1;
    if (pthread_create(&infer_thread, NULL, infer_thread_fn, NULL) != 0) {
        perror("llm_send: pthread_create");
        infer_thread_running = 0;
        queue_push("[error: thread creation failed]", 1);
    }
}

int llm_poll(void) {
    TokenEntry entry;
    while (queue_pop(&entry)) {
        if (current_cb) {
            current_cb(entry.text, entry.done, current_userdata);
            if (entry.done) {
                current_cb = NULL;
                current_userdata = NULL;
            }
        }
    }
    return infer_thread_running;
}

int llm_ready(void) {
    return server_is_ready;
}

void llm_shutdown(void) {
    /* Cancel and join inference thread */
    if (infer_thread_running) {
        infer_cancel = 1;
        pthread_join(infer_thread, NULL);
        infer_thread_running = 0;
    }

    /* Kill the server process */
    if (server_pid > 0) {
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
        server_pid = -1;
    }

    server_is_ready = 0;
    current_cb = NULL;
    current_userdata = NULL;

    /* Drain the queue */
    pthread_mutex_lock(&queue_mutex);
    queue_head = queue_tail = 0;
    pthread_mutex_unlock(&queue_mutex);

    curl_global_cleanup();
    printf("llm_shutdown: cleaned up\n");
}
