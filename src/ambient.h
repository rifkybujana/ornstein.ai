#ifndef AMBIENT_H
#define AMBIENT_H

#include "face.h"

void  ambient_init(void);
void  ambient_update(Emotion emotion, float dt);
float ambient_bob_offset(float time, float scale);
void  ambient_render(float cx, float cy, Emotion emotion, float scale,
                     float fb_w, float fb_h, float time);
void  ambient_cleanup(void);

#endif
