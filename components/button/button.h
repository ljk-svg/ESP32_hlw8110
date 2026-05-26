#ifndef BUTTON_H
#define BUTTON_H

typedef enum {
    KEY_NONE = 0,
    KEY_WIFI,       // GPIO 5
    KEY_UP,         // GPIO 6
    KEY_DOWN        // GPIO 7
} key_event_t;

void Button_Init(void);
key_event_t Button_Scan(void);

#endif