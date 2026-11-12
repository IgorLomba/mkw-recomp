// Switch 2 controllers over Bluetooth LE on Windows (switch2.h): not written yet. The WinRT calls it needs
// (BluetoothLEAdvertisementWatcher, GattDeviceService) have C headers in llvm-mingw; until then MKW_SWITCH2 says so.
#include "cfg.h"
#include "host.h"
#include "switch2.h"

void switch2_start(void) {
    if (cfg_int("MKW_SWITCH2")) rt_log("input: Switch 2 controllers over Bluetooth are not on Windows yet (Linux only)");
}
