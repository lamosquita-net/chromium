// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox/mac/system_services.h"

#include <Carbon/Carbon.h>
#include <CoreFoundation/CoreFoundation.h>

#include "base/apple/osstatus_logging.h"

extern "C" {
OSStatus SetApplicationIsDaemon(Boolean isDaemon);
void _LSSetApplicationLaunchServicesServerConnectionStatus(
    uint64_t flags,
    bool (^connection_allowed)(CFDictionaryRef options));

// See
// https://github.com/WebKit/WebKit/blob/24aaedc770d192d03a07ba4a71727274aaa8fc07/Source/WebKit/WebProcess/cocoa/WebProcessCocoa.mm#L840
void _CSCheckFixDisable() __attribute__((weak_import));  // FlyWeb (LOCAL-6, 21-10): no existe en 10.14
}  // extern "C"

namespace sandbox {

void DisableLaunchServices() {
  // Allow the process to continue without a LaunchServices ASN. The
  // INIT_Process function in HIServices will abort if it cannot connect to
  // launchservicesd to get an ASN. By setting this flag, HIServices skips
  // that.
  OSStatus status = SetApplicationIsDaemon(true);
  OSSTATUS_LOG_IF(ERROR, status != noErr, status) << "SetApplicationIsDaemon";

  // Close any connections to launchservicesd and use an always-false predicate
  // to discourage future attempts to connect.
  _LSSetApplicationLaunchServicesServerConnectionStatus(
      0, ^bool(CFDictionaryRef options) {
        return false;
      });
}

void DisableCoreServicesCheckFix() {
  if (_CSCheckFixDisable) {  // FlyWeb (LOCAL-6, 21-10): débil en 10.14
    _CSCheckFixDisable();
  }
}

}  // namespace sandbox
