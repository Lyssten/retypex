#include "layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * RU↔EN mapping (QWERTY-based Russian layout).
 * Each pair: { ru_codepoint, en_ascii }
 * Lowercase entries — uppercase handled separately (codepoint - 0x20).
 */
typedef struct { uint32_t ru; char en; } Pair;

static const Pair RU_LC[] = {
    { 0x0430, 'f' }, /* а */  { 0x0431, ',' }, /* б */
    { 0x0432, 'd' }, /* в */  { 0x0433, 'u' }, /* г */
    { 0x0434, 'l' }, /* д */  { 0x0435, 't' }, /* е */
    { 0x0451, '`' }, /* ё */  { 0x0436, ';' }, /* ж */
    { 0x0437, 'p' }, /* з */  { 0x0438, 'b' }, /* и */
    { 0x0439, 'q' }, /* й */  { 0x043A, 'r' }, /* к */
    { 0x043B, 'k' }, /* л */  { 0x043C, 'v' }, /* м */
    { 0x043D, 'y' }, /* н */  { 0x043E, 'j' }, /* о */
    { 0x043F, 'g' }, /* п */  { 0x0440, 'h' }, /* р */
    { 0x0441, 'c' }, /* с */  { 0x0442, 'n' }, /* т */
    { 0x0443, 'e' }, /* у */  { 0x0444, 'a' }, /* ф */
    { 0x0445, '[' }, /* х */  { 0x0446, 'w' }, /* ц */
    { 0x0447, 'x' }, /* ч */  { 0x0448, 'i' }, /* ш */
    { 0x0449, 'o' }, /* щ */  { 0x044A, ']' }, /* ъ */
    { 0x044B, 's' }, /* ы */  { 0x044C, 'm' }, /* ь */
    { 0x044D,'\'' }, /* э */  { 0x044E, '.' }, /* ю */
    { 0x044F, 'z' }, /* я */
};

static const Pair RU_UC[] = {
    { 0x0410, 'F' }, /* А */  { 0x0411, '<' }, /* Б */
    { 0x0412, 'D' }, /* В */  { 0x0413, 'U' }, /* Г */
    { 0x0414, 'L' }, /* Д */  { 0x0415, 'T' }, /* Е */
    { 0x0401, '~' }, /* Ё */  { 0x0416, ':' }, /* Ж */
    { 0x0417, 'P' }, /* З */  { 0x0418, 'B' }, /* И */
    { 0x0419, 'Q' }, /* Й */  { 0x041A, 'R' }, /* К */
    { 0x041B, 'K' }, /* Л */  { 0x041C, 'V' }, /* М */
    { 0x041D, 'Y' }, /* Н */  { 0x041E, 'J' }, /* О */
    { 0x041F, 'G' }, /* П */  { 0x0420, 'H' }, /* Р */
    { 0x0421, 'C' }, /* С */  { 0x0422, 'N' }, /* Т */
    { 0x0423, 'E' }, /* У */  { 0x0424, 'A' }, /* Ф */
    { 0x0425, '{' }, /* Х */  { 0x0426, 'W' }, /* Ц */
    { 0x0427, 'X' }, /* Ч */  { 0x0428, 'I' }, /* Ш */
    { 0x0429, 'O' }, /* Щ */  { 0x042A, '}' }, /* Ъ */
    { 0x042B, 'S' }, /* Ы */  { 0x042C, 'M' }, /* Ь */
    { 0x042D, '"' }, /* Э */  { 0x042E, '>' }, /* Ю */
    { 0x042F, 'Z' }, /* Я */
};

#define NELEM(a) ((int)(sizeof(a)/sizeof((a)[0])))

/* Decode one UTF-8 char; advance *s; return codepoint or 0 on error */
static uint32_t utf8_next(const unsigned char **s) {
    unsigned char c = **s;
    if (c == 0) return 0;
    (*s)++;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0) {
        uint32_t cp = (c & 0x1F) << 6;
        cp |= (**s & 0x3F); (*s)++;
        return cp;
    }
    if ((c & 0xF0) == 0xE0) {
        uint32_t cp = (c & 0x0F) << 12;
        cp |= ((**s & 0x3F) << 6); (*s)++;
        cp |= (**s & 0x3F); (*s)++;
        return cp;
    }
    /* 4-byte */
    uint32_t cp = (c & 0x07) << 18;
    cp |= ((**s & 0x3F) << 12); (*s)++;
    cp |= ((**s & 0x3F) << 6);  (*s)++;
    cp |= (**s & 0x3F);          (*s)++;
    return cp;
}

/* Encode codepoint as UTF-8 into buf (must have >= 5 bytes); returns bytes written */
static int utf8_encode(uint32_t cp, char *buf) {
    if (cp < 0x80) { buf[0] = cp; return 1; }
    if (cp < 0x800) {
        buf[0] = 0xC0 | (cp >> 6);
        buf[1] = 0x80 | (cp & 0x3F);
        return 2;
    }
    if (cp < 0x10000) {
        buf[0] = 0xE0 | (cp >> 12);
        buf[1] = 0x80 | ((cp >> 6) & 0x3F);
        buf[2] = 0x80 | (cp & 0x3F);
        return 3;
    }
    buf[0] = 0xF0 | (cp >> 18);
    buf[1] = 0x80 | ((cp >> 12) & 0x3F);
    buf[2] = 0x80 | ((cp >> 6)  & 0x3F);
    buf[3] = 0x80 | (cp & 0x3F);
    return 4;
}

static int is_ru_cp(uint32_t cp) {
    return (cp >= 0x0410 && cp <= 0x044F) || cp == 0x0401 || cp == 0x0451;
}

/* True if cp is any character that appears in the EN side of our mapping tables */
static int is_en_mapped(uint32_t cp) {
    if (cp > 0x7F) return 0;
    for (int i = 0; i < NELEM(RU_LC); i++)
        if ((uint32_t)(unsigned char)RU_LC[i].en == cp) return 1;
    for (int i = 0; i < NELEM(RU_UC); i++)
        if ((uint32_t)(unsigned char)RU_UC[i].en == cp) return 1;
    return 0;
}

int layout_detect(const char *utf8) {
    const unsigned char *s = (const unsigned char *)utf8;
    int ru = 0, en = 0;
    uint32_t cp;
    while ((cp = utf8_next(&s)) != 0) {
        if (is_ru_cp(cp)) ru++;
        else if (is_en_mapped(cp)) en++;
    }
    return (ru >= en) ? 1 : 0;
}

/* ru codepoint → en ascii char, 0 if not found */
static char ru_cp_to_en(uint32_t cp) {
    for (int i = 0; i < NELEM(RU_LC); i++)
        if (RU_LC[i].ru == cp) return RU_LC[i].en;
    for (int i = 0; i < NELEM(RU_UC); i++)
        if (RU_UC[i].ru == cp) return RU_UC[i].en;
    return 0;
}

/* en ascii char → ru UTF-8 string (2-byte), written into out; returns bytes */
static int en_char_to_ru(char c, char *out) {
    for (int i = 0; i < NELEM(RU_LC); i++) {
        if (RU_LC[i].en == c) return utf8_encode(RU_LC[i].ru, out);
    }
    for (int i = 0; i < NELEM(RU_UC); i++) {
        if (RU_UC[i].en == c) return utf8_encode(RU_UC[i].ru, out);
    }
    return 0;
}

char *layout_convert(const char *utf8) {
    if (!utf8 || !*utf8) return strdup("");

    int dir = layout_detect(utf8); /* 1=RU→EN, 0=EN→RU */
    int inlen = strlen(utf8);
    /* worst case: each EN char → 2-byte RU */
    char *out = malloc(inlen * 2 + 4);
    if (!out) return NULL;
    int oi = 0;

    const unsigned char *s = (const unsigned char *)utf8;
    uint32_t cp;
    while ((cp = utf8_next(&s)) != 0) {
        if (dir == 1 && is_ru_cp(cp)) {
            char en = ru_cp_to_en(cp);
            if (en) { out[oi++] = en; continue; }
        } else if (dir == 0 && is_en_mapped(cp)) {
            char ru[4];
            int n = en_char_to_ru((char)cp, ru);
            if (n > 0) { memcpy(out + oi, ru, n); oi += n; continue; }
        }
        /* keep character as-is */
        oi += utf8_encode(cp, out + oi);
    }
    out[oi] = '\0';
    return out;
}
