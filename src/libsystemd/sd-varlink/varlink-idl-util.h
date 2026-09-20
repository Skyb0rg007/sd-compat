/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "sd-varlink.h"
#include "sd-varlink-idl.h"

#include "forward.h"

const sd_varlink_symbol* varlink_idl_find_symbol(const sd_varlink_interface *interface, sd_varlink_symbol_type_t type, const char *name);
const sd_varlink_field* varlink_idl_find_field(const sd_varlink_symbol *symbol, const char *name);

int varlink_idl_validate_method_call(const sd_varlink_symbol *method, sd_json_variant *v, sd_varlink_method_flags_t flags, const char **reterr_bad_field);
int varlink_idl_validate_method_reply(const sd_varlink_symbol *method, sd_json_variant *v, sd_varlink_reply_flags_t flags, const char **reterr_bad_field);
int varlink_idl_validate_error(const sd_varlink_symbol *error, sd_json_variant *v, const char **bad_field);
