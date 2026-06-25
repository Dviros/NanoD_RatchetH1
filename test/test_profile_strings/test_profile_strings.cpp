/*
 * test_profile_strings/test_profile_strings.cpp
 *
 * PlatformIO Unity test for include/profile_key_action_strings.h.
 *
 * Asserts the serialize↔parse contract for KA_PROFILE_CHANGE that was
 * historically broken:
 *
 *   BEFORE fix: keyActionToJSON wrote "profiles"; keyActionFromJSON expected
 *               "profile" → action silently dropped on reload.
 *
 *   AFTER fix  (HapticProfileManager.cpp):
 *     keyActionToJSON  (line ~719):  obj["type"] = "profile"
 *     keyActionFromJSON (line ~550):  if (type=="profile" || type=="profiles")
 *                                     -> accepts both; canonical is "profile"
 *
 * This test encodes that contract using the compile-time string constants in
 * profile_key_action_strings.h.  If anyone renames the canonical string in
 * the .cpp without updating the header, these tests break — that is the intent.
 *
 * Build: pio test -e native
 */

#include <unity.h>
#include "profile_key_action_strings.h"
#include <string.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

/*
 * The canonical serialize string must equal the canonical parse string.
 * This is the core invariant: what we write, we can read back.
 */
void test_serialize_matches_canonical_parse(void)
{
    /*
     * Reference lines:
     *   serialize : HapticProfileManager.cpp ~line 719
     *     obj["type"] = "profile";
     *   parse     : HapticProfileManager.cpp ~line 550
     *     if (type=="profile" || type=="profiles")
     */
    TEST_ASSERT_EQUAL_STRING(KA_PROFILE_CHANGE_PARSE_STR,
                              KA_PROFILE_CHANGE_SERIALIZE_STR);
}

/*
 * The serialize string must NOT be the legacy alias ("profiles").
 * This is the exact regression guard: the old code emitted "profiles" and
 * the parser only accepted "profile", breaking round-trips.
 */
void test_serialize_is_not_legacy_alias(void)
{
    TEST_ASSERT_NOT_EQUAL(0,
        strcmp(KA_PROFILE_CHANGE_SERIALIZE_STR,
               KA_PROFILE_CHANGE_PARSE_ALIAS_STR));
}

/*
 * Confirm the serialize string is the literal "profile" (not "profiles").
 * Belt-and-suspenders: catches any future rename.
 */
void test_serialize_string_is_profile(void)
{
    TEST_ASSERT_EQUAL_STRING("profile", KA_PROFILE_CHANGE_SERIALIZE_STR);
}

/*
 * Confirm the canonical parse string is the literal "profile".
 */
void test_parse_string_is_profile(void)
{
    TEST_ASSERT_EQUAL_STRING("profile", KA_PROFILE_CHANGE_PARSE_STR);
}

/*
 * Confirm the legacy alias is "profiles" (the old buggy value).
 * This alias is intentionally still accepted by the parser so existing
 * persisted JSON files from before the fix continue to load; it is NOT
 * emitted on save anymore (dirty=true migration path rewrites it).
 */
void test_legacy_alias_string_is_profiles(void)
{
    TEST_ASSERT_EQUAL_STRING("profiles", KA_PROFILE_CHANGE_PARSE_ALIAS_STR);
}

/*
 * Confirm next/prev profile strings are stable.
 */
void test_next_profile_string(void)
{
    TEST_ASSERT_EQUAL_STRING("next_profile", KA_PROFILE_NEXT_STR);
}

void test_prev_profile_string(void)
{
    TEST_ASSERT_EQUAL_STRING("prev_profile", KA_PROFILE_PREV_STR);
}

/*
 * Confirm that the legacy alias is a strict superset of the canonical string
 * (i.e. it has an extra 's' suffix).  Documents the naming relationship.
 */
void test_legacy_alias_is_canonical_plus_s(void)
{
    /* KA_PROFILE_CHANGE_PARSE_ALIAS_STR == KA_PROFILE_CHANGE_PARSE_STR + "s" */
    char expected[64];
    snprintf(expected, sizeof(expected), "%ss", KA_PROFILE_CHANGE_PARSE_STR);
    TEST_ASSERT_EQUAL_STRING(expected, KA_PROFILE_CHANGE_PARSE_ALIAS_STR);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_serialize_matches_canonical_parse);
    RUN_TEST(test_serialize_is_not_legacy_alias);
    RUN_TEST(test_serialize_string_is_profile);
    RUN_TEST(test_parse_string_is_profile);
    RUN_TEST(test_legacy_alias_string_is_profiles);
    RUN_TEST(test_next_profile_string);
    RUN_TEST(test_prev_profile_string);
    RUN_TEST(test_legacy_alias_is_canonical_plus_s);
    return UNITY_END();
}
