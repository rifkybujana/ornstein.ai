#ifndef STATS_H
#define STATS_H

#include "face.h"

void  stats_init(void);
void  stats_update(float dt);
void  stats_on_message(void);
void  stats_on_chat_open(float dt);
void  stats_on_mood(Emotion e);
float stats_bond(void);
float stats_joy(void);
float stats_energy(void);
void  stats_save(void);
void  stats_shutdown(void);

#endif
