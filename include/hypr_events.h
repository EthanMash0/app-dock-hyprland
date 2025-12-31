#ifndef HYPR_EVENTS_H
#define HYPR_EVENTS_H

#include "state.h"

// Starts the Hyprland event thread. Safe to call once during app activate.
void hypr_events_start(AppState *st);
void hypr_events_stop(AppState *st);

#endif
