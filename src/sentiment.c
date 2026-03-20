#include "sentiment.h"
#include <string.h>
#include <ctype.h>

static const struct {
    const char *name;
    Emotion emotion;
} mood_map[] = {
    {"neutral",   EMOTION_NEUTRAL},
    {"happy",     EMOTION_HAPPY},
    {"excited",   EMOTION_EXCITED},
    {"surprised", EMOTION_SURPRISED},
    {"sleepy",    EMOTION_SLEEPY},
    {"bored",     EMOTION_BORED},
    {"curious",   EMOTION_CURIOUS},
    {"sad",       EMOTION_SAD},
    {"thinking",  EMOTION_THINKING},
};

#define MOOD_MAP_COUNT (sizeof(mood_map) / sizeof(mood_map[0]))

int sentiment_parse(const char *text, Emotion *out_emotion, int *tag_end) {
    if (!text) return 0;

    const char *p = text;
    while (*p == ' ' || *p == '\n' || *p == '\t') p++;

    if (strncmp(p, "[MOOD:", 6) != 0) return 0;
    p += 6;

    const char *close = strchr(p, ']');
    if (!close) return 0;

    int name_len = (int)(close - p);
    if (name_len <= 0 || name_len > 20) return 0;

    char name[21];
    for (int i = 0; i < name_len; i++)
        name[i] = (char)tolower((unsigned char)p[i]);
    name[name_len] = '\0';

    for (int i = 0; i < (int)MOOD_MAP_COUNT; i++) {
        if (strcmp(name, mood_map[i].name) == 0) {
            if (out_emotion) *out_emotion = mood_map[i].emotion;
            int end = (int)(close - text) + 1;
            /* Skip whitespace after tag */
            while (text[end] == ' ') end++;
            if (tag_end) *tag_end = end;
            return 1;
        }
    }

    return 0;
}
