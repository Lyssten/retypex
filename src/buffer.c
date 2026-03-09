#include "buffer.h"
#include <string.h>

void buf_init(WordBuffer *b) {
    memset(b, 0, sizeof(*b));
}

void buf_push(WordBuffer *b, uint16_t code, bool shift) {
    if (b->len >= WORD_MAX) {
        /* shift left to make room */
        memmove(b->keys, b->keys + 1, (WORD_MAX - 1) * sizeof(KeyEntry));
        b->len = WORD_MAX - 1;
    }
    b->keys[b->len].code  = code;
    b->keys[b->len].shift = shift;
    b->len++;
}

void buf_pop(WordBuffer *b) {
    if (b->len > 0) b->len--;
}

void buf_clear(WordBuffer *b) {
    b->len = 0;
}

int buf_len(const WordBuffer *b) {
    return b->len;
}

const KeyEntry *buf_keys(const WordBuffer *b) {
    return b->keys;
}
