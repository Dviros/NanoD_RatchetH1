/*
 * profile_key_action_strings.h — host-safe constants for key-action type strings.
 *
 * Historic bug (now fixed in HapticProfileManager.cpp):
 *   keyActionToJSON()  emitted  obj["type"] = "profiles"   for KA_PROFILE_CHANGE
 *   keyActionFromJSON() expected type == "profile"
 *   => round-tripped profiles silently dropped the action on next load.
 *
 * After the fix (see HapticProfileManager.cpp HapticProfile::keyActionToJSON,
 * case KA_PROFILE_CHANGE):
 *   serialize: "profile"     (canonical, line ~719)
 *   parse    : "profile" OR "profiles"  (legacy alias, line ~550)
 *
 * This header captures the canonical strings as compile-time constants so the
 * native test suite can assert the contract without pulling in Arduino.h or
 * ArduinoJson.  Any future rename in the .cpp must be reflected here, keeping
 * tests and firmware in sync.
 */

#pragma once

/* Serialize constant — what keyActionToJSON writes for KA_PROFILE_CHANGE */
#define KA_PROFILE_CHANGE_SERIALIZE_STR   "profile"

/* Parse constant(s) — what keyActionFromJSON accepts */
#define KA_PROFILE_CHANGE_PARSE_STR       "profile"
#define KA_PROFILE_CHANGE_PARSE_ALIAS_STR "profiles"   /* legacy; triggers dirty=true */

/* Serialize constant for KA_PROFILE_NEXT / KA_PROFILE_PREV */
#define KA_PROFILE_NEXT_STR   "next_profile"
#define KA_PROFILE_PREV_STR   "prev_profile"
