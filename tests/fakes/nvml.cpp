// SPDX-License-Identifier: MIT
// Test-only library. Never installed or linked into the miner.
#include <cstring>
struct Device {
  unsigned power = 600000, core = 2800, mem = 15000, fan = 70;
};
static Device devices[2];
static unsigned writes = 0, last = 99;
static bool deny = false, missing_temp = false;
static int write(void *h) {
  last = static_cast<Device *>(h) == &devices[0] ? 0 : 1;
  if (deny)
    return 4;
  ++writes;
  return 0;
}
extern "C" {
void test_reset() {
  writes = 0;
  last = 99;
  deny = false;
  missing_temp = false;
}
unsigned test_writes() { return writes; }
unsigned test_last() { return last; }
void test_deny() { deny = true; }
void test_missing_temp() { missing_temp = true; }
int nvmlInit_v2() { return 0; }
int nvmlShutdown() { return 0; }
const char *nvmlErrorString(int code) {
  return code == 4 ? "Insufficient Permissions" : "Not Supported";
}
int nvmlDeviceGetHandleByPciBusId_v2(const char *pci, void **h) {
  if (!std::strcmp(pci, "0000:03:00.0"))
    *h = &devices[0];
  else if (!std::strcmp(pci, "0000:07:00.0"))
    *h = &devices[1];
  else
    return 6;
  return 0;
}
int nvmlDeviceGetTemperature(void *, unsigned, unsigned *v) {
  if (missing_temp)
    return 3;
  *v = 63;
  return 0;
}
int nvmlDeviceGetFanSpeed(void *h, unsigned *v) {
  *v = static_cast<Device *>(h)->fan;
  return 0;
}
int nvmlDeviceGetClockInfo(void *h, unsigned type, unsigned *v) {
  *v = type == 2 ? static_cast<Device *>(h)->mem : static_cast<Device *>(h)->core;
  return 0;
}
int nvmlDeviceGetPowerUsage(void *, unsigned *v) {
  *v = 450000;
  return 0;
}
int nvmlDeviceGetPowerManagementLimit(void *h, unsigned *v) {
  *v = static_cast<Device *>(h)->power;
  return 0;
}
int nvmlDeviceGetPowerManagementDefaultLimit(void *, unsigned *v) {
  *v = 600000;
  return 0;
}
int nvmlDeviceGetPowerManagementLimitConstraints(void *, unsigned *lo, unsigned *hi) {
  *lo = 100000;
  *hi = 600000;
  return 0;
}
int nvmlDeviceGetNumFans(void *, unsigned *v) {
  *v = 2;
  return 0;
}
int nvmlDeviceSetPowerManagementLimit(void *h, unsigned v) {
  auto r = write(h);
  if (!r)
    static_cast<Device *>(h)->power = v;
  return r;
}
int nvmlDeviceSetGpuLockedClocks(void *h, unsigned lo, unsigned) {
  auto r = write(h);
  if (!r)
    static_cast<Device *>(h)->core = lo;
  return r;
}
int nvmlDeviceSetMemoryLockedClocks(void *h, unsigned lo, unsigned) {
  auto r = write(h);
  if (!r)
    static_cast<Device *>(h)->mem = lo;
  return r;
}
int nvmlDeviceResetGpuLockedClocks(void *h) { return write(h); }
int nvmlDeviceResetMemoryLockedClocks(void *h) { return write(h); }
int nvmlDeviceSetFanSpeed_v2(void *h, unsigned, unsigned speed) {
  auto r = write(h);
  if (!r)
    static_cast<Device *>(h)->fan = speed;
  return r;
}
int nvmlDeviceSetDefaultFanSpeed_v2(void *h, unsigned) { return write(h); }
}
