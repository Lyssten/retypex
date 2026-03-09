#pragma once

#include <stdint.h>
#include <stdbool.h>

#define WORD_MAX 256

typedef struct {
    uint16_t code;
    bool     shift;
} KeyEntry;

typedef struct {
    KeyEntry keys[WORD_MAX];
    int      len;
} WordBuffer;

void            buf_init(WordBuffer *b);
void            buf_push(WordBuffer *b, uint16_t code, bool shift);
void            buf_pop(WordBuffer *b);
void            buf_clear(WordBuffer *b);
int             buf_len(const WordBuffer *b);
const KeyEntry *buf_keys(const WordBuffer *b);
