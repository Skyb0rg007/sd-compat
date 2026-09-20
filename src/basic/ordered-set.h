/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "forward.h"
#include "hashmap.h"

typedef struct OrderedSet OrderedSet;

static inline OrderedSet* ordered_set_new(const struct hash_ops *ops) {
        return (OrderedSet*) ordered_hashmap_new(ops);
}

static inline OrderedSet* ordered_set_free(OrderedSet *s) {
        return (OrderedSet*) ordered_hashmap_free((OrderedHashmap*) s);
}

static inline int ordered_set_contains(OrderedSet *s, const void *p) {
        return ordered_hashmap_contains((OrderedHashmap*) s, p);
}

static inline int ordered_set_put(OrderedSet *s, void *p) {
        return ordered_hashmap_put((OrderedHashmap*) s, p, p);
}

static inline bool ordered_set_isempty(OrderedSet *s) {
        return ordered_hashmap_isempty((OrderedHashmap*) s);
}

static inline bool ordered_set_iterate(OrderedSet *s, Iterator *i, void **value) {
        return ordered_hashmap_iterate((OrderedHashmap*) s, i, value, NULL);
}

static inline void* ordered_set_steal_first(OrderedSet *s) {
        return ordered_hashmap_steal_first((OrderedHashmap*) s);
}

static inline char** ordered_set_get_strv(OrderedSet *s) {
        return _hashmap_get_strv(HASHMAP_BASE((OrderedHashmap*) s));
}

int ordered_set_consume(OrderedSet *s, void *p);

#define _ORDERED_SET_FOREACH(e, s, i) \
        for (Iterator i = ITERATOR_FIRST; ordered_set_iterate((s), &i, (void**)&(e)); )
#define ORDERED_SET_FOREACH(e, s) \
        _ORDERED_SET_FOREACH(e, s, UNIQ_T(i, UNIQ))

DEFINE_TRIVIAL_CLEANUP_FUNC(OrderedSet*, ordered_set_free);

#define _cleanup_ordered_set_free_ _cleanup_(ordered_set_freep)
