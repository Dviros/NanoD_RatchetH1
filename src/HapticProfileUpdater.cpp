
#include "./HapticProfileManager.h"
#include "class/hid/hid.h"


// Migration note for KA_PROFILE_CHANGE "profiles"/"profile" string bug:
// Profiles written by firmware before this fix serialised KA_PROFILE_CHANGE key actions
// with type="profiles" instead of the correct type="profile".  keyActionFromJSON now
// accepts both strings as an alias and marks the profile dirty, so the next toSPIFFS()
// call rewrites the file with the correct value.  No explicit version bump is needed for
// this particular fix because dirty=true is set on load, triggering the rewrite.

void HapticProfileManager::updateProfile(HapticProfile* profile, uint8_t from_version) {
    if (from_version==1) {
        // Fix 7: only install the default key mapping when the slot has no
        // user-defined actions, so existing customisations are preserved.
        // Previously all 4 slots were unconditionally overwritten with hardcoded
        // defaults on every v1->v2 migration, destroying user key mappings.
        static const uint8_t default_keys[4] = {HID_KEY_N, HID_KEY_A, HID_KEY_N, HID_KEY_O};
        for (int i = 0; i < 4; i++) {
            if (profile->hmi_config.keys[i].num_pressed_actions == 0) {
                profile->hmi_config.keys[i].num_pressed_actions = 1;
                profile->hmi_config.keys[i].pressed[0].type = keyActionType::KA_KEY;
                profile->hmi_config.keys[i].pressed[0].hid.num = 1;
                profile->hmi_config.keys[i].pressed[0].hid.key_codes[0] = default_keys[i];
            }
        }
        Serial.print("Updated profile ");
        Serial.print(profile->profile_name);
        Serial.println(" from version 1 to 2");
    }
}