// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/obsolete_system/obsolete_system.h"

#include "base/mac/mac_util.h"
#include "chrome/common/chrome_version.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/branded_strings.h"
#include "ui/base/l10n/l10n_util.h"

// Chromium 150 will be the last mstone to support macOS 12.
[[maybe_unused]] constexpr int kLastMStoneWithSupport = 150;  // FlyWeb B1d
[[maybe_unused]] constexpr int kMacOSReleaseBeingObsoleted = 12;  // FlyWeb B1d

namespace ObsoleteSystem {

bool IsObsoleteNowOrSoon() {
  // FlyWeb B1d: FlyWeb supports macOS 10.14 and later, so never show the
  // "this computer will soon stop receiving updates" infobar or the
  // IDS_MACOS_OBSOLETE notice in About.
  return false;
}

std::u16string LocalizedObsoleteString() {
  return l10n_util::GetStringUTF16(IDS_MACOS_OBSOLETE);
}

bool IsEndOfTheLine() {
  return false;  // FlyWeb B1d (was CHROME_VERSION_MAJOR >= 150)
}

const char* GetLinkURL() {
  return chrome::kMacOsObsoleteURL;
}

}  // namespace ObsoleteSystem
