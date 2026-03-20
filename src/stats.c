#include "stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "util.h"

static float s_bond   = 0.0f;
static float s_joy    = 0.5f;
static float s_energy = 1.0f;
static float s_save_timer = 0.0f;

#define SAVE_INTERVAL 60.0f

static float json_get_float(const char *json, const char *key, float def) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return def;
    p += strlen(pattern);
    while (*p == ' ') p++;
    return (float)atof(p);
}

static long json_get_long(const char *json, const char *key, long def) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return def;
    p += strlen(pattern);
    while (*p == ' ') p++;
    return atol(p);
}

static char stats_path[1024];

static void ensure_path(void) {
    if (stats_path[0]) return;
    const char *home = getenv("HOME");
    snprintf(stats_path, sizeof(stats_path), "%s/.ornstein/stats.json",
             home ? home : ".");
}

void stats_init(void) {
    ensure_path();

    FILE *fp = fopen(stats_path, "r");
    if (!fp) return;

    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';
    fclose(fp);

    s_bond   = json_get_float(buf, "bond",   0.0f);
    s_joy    = json_get_float(buf, "joy",    0.5f);
    s_energy = json_get_float(buf, "energy", 1.0f);
    long last_save = json_get_long(buf, "last_save", 0);

    if (last_save > 0) {
        long now = (long)time(NULL);
        float hours = (float)(now - last_save) / 3600.0f;
        if (hours > 0.0f) {
            s_bond   -= 0.01f * hours;
            s_joy    -= 0.02f * hours;
            s_energy -= 0.05f * hours;
            s_bond   = clampf(s_bond,   0.0f, 1.0f);
            s_joy    = clampf(s_joy,    0.0f, 1.0f);
            s_energy = clampf(s_energy, 0.0f, 1.0f);
        }
    }

    s_save_timer = SAVE_INTERVAL;
}

void stats_update(float dt) {
    s_energy += 0.003f * dt;
    s_energy = clampf(s_energy, 0.0f, 1.0f);

    s_save_timer -= dt;
    if (s_save_timer <= 0.0f) {
        s_save_timer = SAVE_INTERVAL;
        stats_save();
    }
}

void stats_on_message(void) {
    s_bond   += 0.02f;
    s_energy -= 0.01f;
    s_bond   = clampf(s_bond,   0.0f, 1.0f);
    s_energy = clampf(s_energy, 0.0f, 1.0f);
}

void stats_on_chat_open(float dt) {
    s_bond += 0.005f * dt / 60.0f;
    s_bond = clampf(s_bond, 0.0f, 1.0f);
}

void stats_on_mood(Emotion e) {
    switch (e) {
        case EMOTION_HAPPY:
        case EMOTION_EXCITED:
            s_joy += 0.05f;
            break;
        case EMOTION_SAD:
            s_joy -= 0.03f;
            break;
        default:
            break;
    }
    s_joy = clampf(s_joy, 0.0f, 1.0f);
}

float stats_bond(void)   { return s_bond; }
float stats_joy(void)    { return s_joy; }
float stats_energy(void) { return s_energy; }

void stats_save(void) {
    ensure_path();

    char dir[1024];
    strncpy(dir, stats_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755);
    }

    FILE *fp = fopen(stats_path, "w");
    if (!fp) return;
    fprintf(fp, "{\"bond\": %.4f, \"joy\": %.4f, \"energy\": %.4f, \"last_save\": %ld}\n",
            s_bond, s_joy, s_energy, (long)time(NULL));
    fclose(fp);
}

void stats_shutdown(void) {
    stats_save();
}
