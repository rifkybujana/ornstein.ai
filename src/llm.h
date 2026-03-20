#ifndef LLM_H
#define LLM_H

/* Token callback: called on the main thread from llm_poll().
   token is a null-terminated string fragment.
   thinking=1 means this token is part of the model's reasoning.
   done=1 means generation is complete. */
typedef void (*llm_token_cb)(const char *token, int thinking, int done, void *userdata);

/* Initialize the LLM subsystem.
   model_path: path to GGUF model file.
   server_path: path to llama-server binary.
   Returns 0 on success, -1 if server fails to start. */
int llm_init(const char *model_path, const char *server_path);

/* Send a chat message. The response streams via the callback set in llm_poll.
   mood: current emotion name string (e.g. "happy", "sleepy").
   user_msg: the user's message.
   cb: called from llm_poll with each token.
   userdata: passed to cb. */
void llm_send(const char *mood, const char *user_msg,
              llm_token_cb cb, void *userdata);

/* Call every frame from the main loop. Dispatches any pending tokens
   to the registered callback. Returns 1 if currently generating. */
int llm_poll(void);

/* Returns 1 if the server is ready (health check passed). */
int llm_ready(void);

/* Returns download state:
   0 = no download needed / ready
   1 = downloading (percent, mb_done, mb_total set)
   2 = download complete, starting server
   3 = server starting (health polling)
  -1 = download/start failed (msg set) */
int llm_status(int *percent, float *mb_done, float *mb_total,
               char *msg, int msg_size);

/* Shutdown: kill server, join threads, free resources. */
void llm_shutdown(void);

#endif
