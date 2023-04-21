#ifndef TLSC_EVENT_H
#define TLSC_EVENT_H

#include "decl.h"

typedef void (*EventHandler)(void *receiver, void *sender, void *args);

C_CLASS_DECL(Event);

Event *Event_create(void *sender) ATTR_RETNONNULL;
void Event_register(Event *self, void *receiver,
	EventHandler handler, int id) CMETHOD ATTR_NONNULL((3));
void Event_unregister(Event *self, void *receiver,
	EventHandler handler, int id) CMETHOD ATTR_NONNULL((3));
void Event_raise(Event *self, int id, void *args) CMETHOD;
void Event_destroy(Event *self);

#endif
