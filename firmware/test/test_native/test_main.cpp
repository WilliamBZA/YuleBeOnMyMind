// Single Unity runner for the whole yule_core test suite. Each module has a test_<module>.cpp that exposes a
// run_<module>_tests() group; add new groups here.
#include <unity.h>

#include "yule_core.h"

void setUp() {}
void tearDown() {}

void run_rgb_tests();
void run_rtttl_tests();
void run_melody_tests();
void run_effects_tests();
void run_compositor_tests();
void run_messages_tests();
void run_topics_tests();
void run_controls_tests();
void run_settings_tests();
void run_ota_tests();
void run_network_tests();

static void test_core_library_is_linked() { TEST_ASSERT_EQUAL_STRING("yule_core", yule::kCoreName); }

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_core_library_is_linked);
  run_rgb_tests();
  run_rtttl_tests();
  run_melody_tests();
  run_effects_tests();
  run_compositor_tests();
  run_messages_tests();
  run_topics_tests();
  run_controls_tests();
  run_settings_tests();
  run_ota_tests();
  run_network_tests();
  return UNITY_END();
}
