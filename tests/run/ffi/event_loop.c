// A toolkit's event loop, as a GUI, audio or game library has one
// (ADR 0099): the library owns the loop and the pixels, and calls back.

#include <stdint.h>

typedef struct {
    int32_t kind;   // 0 resize, 1 key, 2 frame, 3 close
    int32_t key;
    int32_t width;
    int32_t height;
} tk_event;

typedef int32_t (*tk_handler)(void *data, const tk_event *event, uint32_t *pixels);

static uint32_t pixels[8 * 8];

// Delivers a fixed list of events until the handler returns 1, and
// returns the number delivered times 1000 plus a sum of the pixels.
int64_t tk_run(tk_handler handler, void *data) {
    const tk_event events[] = {
        {0, 0, 4, 3}, {1, 'r', 0, 0}, {2, 0, 4, 3}, {0, 0, 8, 8},
        {1, 'g', 0, 0}, {2, 0, 8, 8}, {1, 'q', 0, 0}, {3, 0, 0, 0},
    };
    int64_t delivered = 0;
    int32_t width = 0;
    int32_t height = 0;
    for (int i = 0; i < 8; i++) {
        if (events[i].kind == 0) {
            width = events[i].width;
            height = events[i].height;
        }
        delivered++;
        if (handler(data, &events[i], pixels) == 1) {
            break;
        }
    }
    int64_t sum = 0;
    for (int32_t i = 0; i < width * height; i++) {
        sum += pixels[i] & 0xff;
    }
    return delivered * 1000 + sum;
}
