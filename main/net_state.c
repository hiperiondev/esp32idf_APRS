#include <stdatomic.h>

#include "net_state.h"

static atomic_bool s_connected = false;

void net_state_init(void) {
    atomic_store(&s_connected, false);
}

void net_state_set_connected(bool connected) {
    atomic_store(&s_connected, connected);
}

bool net_state_is_connected(void) {
    return atomic_load(&s_connected);
}
