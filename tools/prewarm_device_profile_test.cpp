// Linux/CI runner for the prewarmer's device-profile checks. No GPU needed.
#include "prewarm_device_profile_selftest.h"

int main() {
    return KytyPrewarm::DeviceProfileSelfTest() ? 0 : 1;
}
