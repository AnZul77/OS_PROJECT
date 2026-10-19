
#ifndef COLLECTOR_H
#define COLLECTOR_H

#include <stddef.h>
#include <stdint.h>
#include "event.h"

/*
 * Initialise and shut down the event collector.
 */
int collector_init(void);
void collector_shutdown(void);

/*
 * Record one event.
 *
 * category: event category defined in event.h
 * type:     event type defined in event.h
 * addr:     related memory address, if any
 * size:     related size or byte count
 * status:   operation result, file descriptor, or error code
 * label:    short description of the event
 */
void collector_record_event(
    event_category_t category,
    event_type_t type,
    uintptr_t addr,
    size_t size,
    int32_t status,
    const char *label
);

#endif
