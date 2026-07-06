#ifndef UTIL_FIFO_H
#define UTIL_FIFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RB_ATOMIC_START {
#define RB_ATOMIC_END   }

typedef struct FIFOBuffer {
    uint16_t *begin;
    uint16_t *end;
    uint16_t *head;
    uint16_t *tail;
} FIFOBuffer;

static inline bool fifo_isempty(const FIFOBuffer *f) {
    return f->head == f->tail;
}

static inline bool fifo_isfull(const FIFOBuffer *f) {
    return ((f->head == f->begin) && (f->tail == f->end)) || (f->tail == f->head - 1);
}

static inline void fifo_push(FIFOBuffer *f, uint16_t c) {
    *(f->tail) = c;

    if (f->tail >= f->end) {
        f->tail = f->begin;
    } else {
        f->tail++;
    }
}

static inline uint16_t fifo_pop(FIFOBuffer *f) {
    uint16_t c;
    if (f->head >= f->end) {
        f->head = f->begin;
        return *(f->end);
    } else {
        c = *(f->head);
        if (!(f->head == f->tail))
            f->head++;
        return c;
    }
}

static inline void fifo_flush(FIFOBuffer *f) {
    f->head = f->tail = f->begin;
}

static inline bool fifo_isempty_locked(const FIFOBuffer *f) {
    bool result;
    RB_ATOMIC_START {
        result = fifo_isempty(f);
    }
    RB_ATOMIC_END
    return result;
}

static inline bool fifo_isfull_locked(const FIFOBuffer *f) {
    bool result;
    RB_ATOMIC_START {
        result = fifo_isfull(f);
    }
    RB_ATOMIC_END
    return result;
}

static inline void fifo_push_locked(FIFOBuffer *f, uint16_t c) {
    RB_ATOMIC_START {
        fifo_push(f, c);
    }
    RB_ATOMIC_END
}

static inline uint16_t fifo_pop_locked(FIFOBuffer *f) {
    uint16_t c;
    RB_ATOMIC_START {
        c = fifo_pop(f);
    }
    RB_ATOMIC_END
    return c;
}

static inline void fifo_init(FIFOBuffer *f, uint16_t *buffer, size_t size) {
    f->head = f->tail = f->begin = buffer;
    f->end = buffer + size - 1;
}

static inline size_t fifo_len(FIFOBuffer *f) {
    if (f->tail > f->head) {
        return f->tail - f->head;
    } else {
        return (f->end - f->head) + (f->tail - f->begin);
    }
    return 0;
}

#endif
