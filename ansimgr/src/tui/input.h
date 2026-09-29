#ifndef ANSIMGR_INPUT_H
#define ANSIMGR_INPUT_H

#include <stddef.h>

typedef enum {
    KEY_NONE = 0,
    KEY_CHAR,
    KEY_ENTER,
    KEY_ESC,
    KEY_TAB,
    KEY_BACKTAB,
    KEY_BACKSPACE,
    KEY_DELETE,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PGUP,
    KEY_PGDN,
    KEY_INSERT,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12
} KeyType;

typedef struct {
    KeyType type;
    char ch[5]; /* meaningful when type == KEY_CHAR; full UTF-8 sequence */
} Key;

/* Initialise the decoder on the given (raw-mode) input fd. */
void input_init(int fd);
/*
 * Return the next key. Waits up to timeout_ms for the first byte; if bytes
 * arrive but form an incomplete escape sequence, waits a further 40ms before
 * resolving a bare ESC.
 */
Key input_next(int timeout_ms);

#endif /* ANSIMGR_INPUT_H */
