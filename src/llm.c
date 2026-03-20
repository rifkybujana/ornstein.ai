#include "llm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <curl/curl.h>

/* ── Model auto-download (async) ─────────────────────────────────── */

#define MODEL_URL "https://huggingface.co/bartowski/google_gemma-3-4b-it-GGUF/resolve/main/google_gemma-3-4b-it-Q4_K_M.gguf"

/* Status values (shared with main thread via llm_status) */
enum {
    LLM_STATE_IDLE          = 0,
    LLM_STATE_DOWNLOADING   = 1,
    LLM_STATE_DL_DONE       = 2,
    LLM_STATE_SERVER_START  = 3,
    LLM_STATE_FAILED        = -1,
};

static pthread_mutex_t  state_mutex = PTHREAD_MUTEX_INITIALIZER;
static int              llm_state   = LLM_STATE_IDLE;
static int              dl_percent  = 0;
static float            dl_now_mb   = 0.0f;
static float            dl_total_mb = 0.0f;
static char             state_msg[128] = "";

static void set_state(int s) {
    pthread_mutex_lock(&state_mutex);
    llm_state = s;
    pthread_mutex_unlock(&state_mutex);
}

static void set_state_msg(int s, const char *msg) {
    pthread_mutex_lock(&state_mutex);
    llm_state = s;
    if (msg) {
        strncpy(state_msg, msg, sizeof(state_msg) - 1);
        state_msg[sizeof(state_msg) - 1] = '\0';
    } else {
        state_msg[0] = '\0';
    }
    pthread_mutex_unlock(&state_mutex);
}

static int dl_progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                           curl_off_t ultotal, curl_off_t ulnow) {
    (void)clientp; (void)ultotal; (void)ulnow;
    if (dltotal > 0) {
        int pct = (int)(dlnow * 100 / dltotal);
        double mb_now = (double)dlnow / (1024.0 * 1024.0);
        double mb_total = (double)dltotal / (1024.0 * 1024.0);
        pthread_mutex_lock(&state_mutex);
        dl_percent = pct;
        dl_now_mb = (float)mb_now;
        dl_total_mb = (float)mb_total;
        pthread_mutex_unlock(&state_mutex);
        fprintf(stderr, "\rDownloading model: %.0f/%.0f MB (%d%%)", mb_now, mb_total, pct);
        fflush(stderr);
    }
    return 0;
}

/* Download the GGUF model to model_path with retry + resume.
   Returns 0 on success, -1 on failure. */
#define DL_MAX_RETRIES 10

static int download_model(const char *model_path) {
    char dir[1024];
    strncpy(dir, model_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dir, 0755);
    }

    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.download", model_path);

    fprintf(stderr, "Model not found at %s\n", model_path);
    fprintf(stderr, "Downloading Gemma 3 4B-IT (Q4_0, ~2.5 GB)...\n");

    for (int attempt = 0; attempt < DL_MAX_RETRIES; attempt++) {
        /* Open in append mode for resume, or write mode for first attempt */
        long resume_from = 0;
        FILE *fp;

        if (attempt > 0) {
            /* Check existing file size for resume */
            FILE *check = fopen(tmp_path, "rb");
            if (check) {
                fseek(check, 0, SEEK_END);
                resume_from = ftell(check);
                fclose(check);
            }
            fprintf(stderr, "Retry %d/%d (resuming from %ld bytes)...\n",
                    attempt, DL_MAX_RETRIES - 1, resume_from);
            fp = fopen(tmp_path, "ab");
        } else {
            fp = fopen(tmp_path, "wb");
        }

        if (!fp) {
            fprintf(stderr, "Failed to open %s for writing\n", tmp_path);
            return -1;
        }

        CURL *curl = curl_easy_init();
        if (!curl) {
            fclose(fp);
            unlink(tmp_path);
            return -1;
        }

        curl_easy_setopt(curl, CURLOPT_URL, MODEL_URL);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, dl_progress_cb);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "ornstein/1.0");
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1000L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

        if (resume_from > 0) {
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE,
                             (curl_off_t)resume_from);
        }

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        fclose(fp);

        if (res == CURLE_OK) {
            fprintf(stderr, "\n");
            if (rename(tmp_path, model_path) != 0) {
                perror("rename");
                unlink(tmp_path);
                return -1;
            }
            fprintf(stderr, "Model downloaded to %s\n", model_path);
            return 0;
        }

        fprintf(stderr, "\nDownload error: %s\n", curl_easy_strerror(res));

        /* Only retry on network errors, not HTTP errors */
        if (res != CURLE_RECV_ERROR && res != CURLE_PARTIAL_FILE &&
            res != CURLE_COULDNT_CONNECT && res != CURLE_OPERATION_TIMEDOUT &&
            res != CURLE_GOT_NOTHING && res != CURLE_SEND_ERROR) {
            break;
        }

        sleep(2);
    }

    fprintf(stderr, "Download failed after %d attempts\n", DL_MAX_RETRIES);
    unlink(tmp_path);
    return -1;
}

/* ── Token queue ──────────────────────────────────────────────────── */

#define TOKEN_QUEUE_SIZE 256
#define TOKEN_MAX_LEN    64

typedef struct {
    char text[TOKEN_MAX_LEN];
    int  thinking;
    int  done;
} TokenEntry;

static TokenEntry       token_queue[TOKEN_QUEUE_SIZE];
static int              queue_head, queue_tail;
static pthread_mutex_t  queue_mutex = PTHREAD_MUTEX_INITIALIZER;

static void queue_push(const char *text, int thinking, int done) {
    pthread_mutex_lock(&queue_mutex);
    int next = (queue_tail + 1) % TOKEN_QUEUE_SIZE;
    if (next != queue_head) {
        strncpy(token_queue[queue_tail].text, text, TOKEN_MAX_LEN - 1);
        token_queue[queue_tail].text[TOKEN_MAX_LEN - 1] = '\0';
        token_queue[queue_tail].thinking = thinking;
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

static char request_body[2048];

/* ── Background init thread (download + server start) ─────────────── */

static pthread_t init_thread;
static int       init_thread_running = 0;

static int start_server(void) {
    set_state(LLM_STATE_SERVER_START);

    pid_t pid = fork();
    if (pid < 0) {
        perror("llm: fork");
        return -1;
    }

    if (pid == 0) {
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        execl(saved_server_path, "llama-server",
              "-m", saved_model_path,
              "--port", "8231",
              "-ngl", "99",
              "-c", "2048",
              "--log-disable",
              (char *)NULL);
        _exit(127);
    }

    server_pid = pid;
    printf("llm: spawned llama-server (pid %d)\n", pid);

    /* Poll health endpoint (max 30s) */
    CURL *curl = curl_easy_init();
    if (!curl) {
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
        server_pid = -1;
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8231/health");
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    int ready = 0;
    for (int attempt = 0; attempt < 120; attempt++) {   /* 120 * 500ms = 60s */
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code == 200) {
                ready = 1;
                break;
            }
        }
        usleep(500000);

        int status;
        pid_t w = waitpid(server_pid, &status, WNOHANG);
        if (w > 0) {
            fprintf(stderr, "llm: llama-server exited prematurely "
                    "(status %d)\n", WEXITSTATUS(status));
            curl_easy_cleanup(curl);
            server_pid = -1;
            return -1;
        }
    }

    curl_easy_cleanup(curl);

    if (!ready) {
        fprintf(stderr, "llm: health check timed out\n");
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
        server_pid = -1;
        return -1;
    }

    return 0;
}

static void *init_thread_fn(void *arg) {
    (void)arg;

    /* Download model if needed */
    if (access(saved_model_path, F_OK) != 0) {
        set_state(LLM_STATE_DOWNLOADING);
        if (download_model(saved_model_path) != 0) {
            set_state_msg(LLM_STATE_FAILED, "Download failed");
            init_thread_running = 0;
            return NULL;
        }
        set_state(LLM_STATE_DL_DONE);
    }

    /* Start server */
    if (start_server() != 0) {
        set_state_msg(LLM_STATE_FAILED, "Server failed to start");
        init_thread_running = 0;
        return NULL;
    }

    server_is_ready = 1;
    set_state(LLM_STATE_IDLE);
    printf("llm: ready\n");
    fflush(stdout);
    init_thread_running = 0;
    return NULL;
}

/* ── curl write callback for SSE streaming ────────────────────────── */

/* Extract a JSON string value by key. Returns length written, 0 if not found. */
static int extract_json_str(const char *json, const char *key,
                            char *out, int out_size) {
    /* Build exact pattern with comma/brace delimiter before the key quote */
    const char *p = json;
    int keylen = (int)strlen(key);

    while (*p) {
        /* Find next '"' that could start our key */
        const char *q = strchr(p, '"');
        if (!q) return 0;

        /* Check the key name matches exactly */
        if (strncmp(q + 1, key, keylen) == 0 && q[1 + keylen] == '"') {
            /* Ensure this is a standalone key (preceded by { , or whitespace, not part of longer key) */
            if (q > json) {
                char prev = *(q - 1);
                if (prev != '{' && prev != ',' && prev != ' ' && prev != '\t' && prev != '\n') {
                    p = q + 1;
                    continue;
                }
            }
            /* Found key — now find ":"<value>" */
            const char *colon = q + 1 + keylen + 1; /* past closing " */
            while (*colon == ' ') colon++;
            if (*colon != ':') { p = q + 1; continue; }
            colon++;
            while (*colon == ' ') colon++;
            if (*colon != '"') return 0; /* value is null or not a string */
            p = colon + 1; /* past opening " of value */
            break;
        }
        p = q + 1;
    }
    if (!*p) return 0;

    int i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;
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
    return i;
}

typedef struct {
    char buf[4096];
    int  len;
} SSEBuffer;

static size_t sse_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    SSEBuffer *sse = (SSEBuffer *)userdata;

    for (size_t i = 0; i < total; i++) {
        if (infer_cancel) return 0;

        char c = ptr[i];
        if (c == '\n') {
            sse->buf[sse->len] = '\0';

            if (strncmp(sse->buf, "data: [DONE]", 12) == 0) {
                queue_push("", 0, 1);
            } else if (strncmp(sse->buf, "data: ", 6) == 0) {
                const char *json = sse->buf + 6;
                char tok[TOKEN_MAX_LEN];
                if (extract_json_str(json, "reasoning_content", tok, TOKEN_MAX_LEN) > 0) {
                    queue_push(tok, 1, 0);
                }
                if (extract_json_str(json, "content", tok, TOKEN_MAX_LEN) > 0) {
                    queue_push(tok, 0, 0);
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
        queue_push("[error: curl init failed]", 0, 1);
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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L); /* no total timeout */
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 120L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK && !infer_cancel) {
        char errmsg[128];
        snprintf(errmsg, sizeof(errmsg), "[error: %s]",
                 curl_easy_strerror(res));
        queue_push(errmsg, 0, 1);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    infer_thread_running = 0;
    return NULL;
}

/* ── Build JSON request body ──────────────────────────────────────── */

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
             "Keep responses under 2 sentences. "
             "Before your response, tag your mood: [MOOD:emotion]. "
             "Valid emotions: neutral, happy, excited, surprised, "
             "sleepy, bored, curious, sad, thinking.", mood);

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
             "\"stream\":true,"
             "\"temperature\":0.7,"
             "\"max_tokens\":256"
             "}",
             escaped_system, escaped_user);
}

/* ── Public API ───────────────────────────────────────────────────── */

int llm_init(const char *model_path, const char *server_path) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    strncpy(saved_model_path, model_path, sizeof(saved_model_path) - 1);
    saved_model_path[sizeof(saved_model_path) - 1] = '\0';
    strncpy(saved_server_path, server_path, sizeof(saved_server_path) - 1);
    saved_server_path[sizeof(saved_server_path) - 1] = '\0';

    /* Set initial state so the overlay shows immediately */
    if (access(model_path, F_OK) != 0)
        set_state(LLM_STATE_DOWNLOADING);
    else
        set_state(LLM_STATE_SERVER_START);

    /* Launch background thread for download + server start */
    init_thread_running = 1;
    if (pthread_create(&init_thread, NULL, init_thread_fn, NULL) != 0) {
        perror("llm_init: pthread_create");
        init_thread_running = 0;
        return -1;
    }

    return 0; /* always succeed — init happens in background */
}

void llm_send(const char *mood, const char *user_msg,
              llm_token_cb cb, void *userdata) {
    if (!server_is_ready) return;

    if (infer_thread_running) {
        infer_cancel = 1;
        pthread_join(infer_thread, NULL);
        infer_thread_running = 0;
        infer_cancel = 0;

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
        queue_push("[error: thread creation failed]", 0, 1);
    }
}

int llm_poll(void) {
    TokenEntry entry;
    while (queue_pop(&entry)) {
        if (current_cb) {
            current_cb(entry.text, entry.thinking, entry.done, current_userdata);
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

int llm_status(int *percent, float *mb_done, float *mb_total,
               char *msg, int msg_size) {
    pthread_mutex_lock(&state_mutex);
    int s = llm_state;
    if (percent)  *percent  = dl_percent;
    if (mb_done)  *mb_done  = dl_now_mb;
    if (mb_total) *mb_total = dl_total_mb;
    if (msg && msg_size > 0) {
        strncpy(msg, state_msg, msg_size - 1);
        msg[msg_size - 1] = '\0';
    }
    pthread_mutex_unlock(&state_mutex);
    return s;
}

void llm_shutdown(void) {
    /* Wait for init thread to finish */
    if (init_thread_running) {
        pthread_join(init_thread, NULL);
        init_thread_running = 0;
    }

    if (infer_thread_running) {
        infer_cancel = 1;
        pthread_join(infer_thread, NULL);
        infer_thread_running = 0;
    }

    if (server_pid > 0) {
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
        server_pid = -1;
    }

    server_is_ready = 0;
    current_cb = NULL;
    current_userdata = NULL;

    pthread_mutex_lock(&queue_mutex);
    queue_head = queue_tail = 0;
    pthread_mutex_unlock(&queue_mutex);

    curl_global_cleanup();
    printf("llm_shutdown: cleaned up\n");
}
