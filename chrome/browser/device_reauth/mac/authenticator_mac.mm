// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/device_reauth/mac/authenticator_mac.h"

#import <LocalAuthentication/LocalAuthentication.h>

#include "chrome/browser/password_manager/password_manager_util_mac.h"

AuthenticatorMac::AuthenticatorMac() = default;

AuthenticatorMac::~AuthenticatorMac() = default;

bool AuthenticatorMac::CheckIfBiometricsAvailable() {
  LAContext* context = [[LAContext alloc] init];
  // FYI, these two LAPolicy constants are defined to be the same value (4), but
  // because the new constant has an annotation of macOS 15+ and the old
  // constant is marked as being deprecated, a runtime switch is forced.
  if (@available(macOS 15, *)) {
    return
        [context canEvaluatePolicy:
                     LAPolicyDeviceOwnerAuthenticationWithBiometricsOrCompanion
                             error:nil];
  } else if (@available(macOS 10.15, *)) {
    return [context
        canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometricsOrWatch
                    error:nil];
  } else {
    // FlyWeb B1b: policy 4 is macOS 10.15+; 10.14 throws on it.
    return [context
        canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics
                    error:nil];
  }
}

bool AuthenticatorMac::CheckIfBiometricsOrScreenLockAvailable() {
  LAContext* context = [[LAContext alloc] init];
  return [context canEvaluatePolicy:LAPolicyDeviceOwnerAuthentication
                              error:nil];
}

bool AuthenticatorMac::AuthenticateUserWithNonBiometrics(
    const std::u16string& message) {
  return password_manager_util_mac::AuthenticateUser(message);
}
