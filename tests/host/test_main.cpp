#include <cstdio>

#include "test_harness.h"

void runHttpDateTests();
void runPropfindTests();
void runPathAndPlanTests();
void runProgressJsonTests();
void runProgressLocatorTests();
void runProgressBridgeTests();

int main() {
  std::printf("AnxSync host tests (pure logic, no Arduino dependency)\n\n");
  runHttpDateTests();
  runPropfindTests();
  runPathAndPlanTests();
  runProgressJsonTests();
  runProgressLocatorTests();
  runProgressBridgeTests();
  return anxtest::summary();
}
