#ifndef SENTIMENT_H
#define SENTIMENT_H

#include "face.h"

/* Parse a [MOOD:xxx] tag from the start of text.
   Returns 1 if found, sets *out_emotion and *tag_end (index past the tag).
   Returns 0 if no tag found. */
int sentiment_parse(const char *text, Emotion *out_emotion, int *tag_end);

#endif
