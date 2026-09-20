/* SPDX-License-Identifier: LGPL-2.1-or-later */


#include "hash-funcs.h"
#include "siphash24.h"
#include "strv.h"

void string_hash_func(const char *p, struct siphash *state) {
        siphash24_compress(p, strlen(p) + 1, state);
}

DEFINE_HASH_OPS(string_hash_ops,
                char, string_hash_func, string_compare_func);
DEFINE_HASH_OPS_WITH_KEY_DESTRUCTOR(
                string_hash_ops_free,
                char, string_hash_func, string_compare_func, free);
void trivial_hash_func(const void *p, struct siphash *state) {
        siphash24_compress_typesafe(p, state);
}

int trivial_compare_func(const void *a, const void *b) {
        return CMP(a, b);
}

DEFINE_HASH_OPS(trivial_hash_ops,
                void, trivial_hash_func, trivial_compare_func);
void uint64_hash_func(const uint64_t *p, struct siphash *state) {
        assert(p);

        siphash24_compress_typesafe(*p, state);
}

int uint64_compare_func(const uint64_t *a, const uint64_t *b) {
        assert(a);
        assert(b);

        return CMP(*a, *b);
}

DEFINE_HASH_OPS(uint64_hash_ops,
                uint64_t, uint64_hash_func, uint64_compare_func);
DEFINE_HASH_OPS_WITH_VALUE_DESTRUCTOR(
                uint64_hash_ops_value_free,
                uint64_t, uint64_hash_func, uint64_compare_func,
                void, free);
