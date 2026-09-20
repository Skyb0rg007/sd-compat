/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "forward.h"
#include "hashmap.h"

#define set_free_and_replace(a, b)              \
        free_and_replace_full(a, b, set_free)

static inline Set* set_free(Set *s) {
        return (Set*) _hashmap_free(HASHMAP_BASE(s));
}

int set_ensure_allocated(Set **s, const struct hash_ops *hash_ops);

int set_put(Set *s, const void *key);
/* no set_update */
/* no set_replace */
static inline void *set_get(const Set *s, const void *key) {
        return _hashmap_get(HASHMAP_BASE((Set *) s), key);
}
/* no set_get2 */

static inline void *set_remove(Set *s, const void *key) {
        return _hashmap_remove(HASHMAP_BASE(s), key);
}

/* no set_remove2 */
/* no set_remove_value */
/* no set_remove_and_replace */

static inline unsigned set_size(const Set *s) {
        return _hashmap_size(HASHMAP_BASE((Set *) s));
}

static inline bool set_isempty(const Set *s) {
        return set_size(s) == 0;
}

static inline bool set_iterate(const Set *s, Iterator *i, void **value) {
        return _hashmap_iterate(HASHMAP_BASE((Set*) s), i, value, NULL);
}

/* no set_steal_first_key */
/* no set_first_key */

static inline void *set_first(const Set *s) {
        return _hashmap_first_key_and_value(HASHMAP_BASE((Set *) s), false, NULL);
}

/* no set_next */

int set_ensure_put(Set **s, const struct hash_ops *hash_ops, const void *key);

#define _SET_FOREACH(e, s, i) \
        for (Iterator i = ITERATOR_FIRST; set_iterate((s), &i, (void**)&(e)); )
#define SET_FOREACH(e, s) \
        _SET_FOREACH(e, s, UNIQ_T(i, UNIQ))

DEFINE_TRIVIAL_CLEANUP_FUNC(Set*, set_free);

#define _cleanup_set_free_ _cleanup_(set_freep)
