// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/installer/gcapi/google_update_util.h"

#include <windows.h>

#include "base/win/registry.h"
#include "chrome/installer/util/google_update_constants.h"

namespace gcapi_internals {

#if defined(OFFICIAL_BUILD)
const wchar_t kChromeRegClientsKey[] =
    L"Software\\BraveSoftware\\Update\\Clients\\"
    L"{AFE6A462-C574-4B8A-AF43-4CC60DF4563B}";
const wchar_t kChromeRegClientStateKey[] =
    L"Software\\BraveSoftware\\Update\\ClientState\\"
    L"{AFE6A462-C574-4B8A-AF43-4CC60DF4563B}";
const wchar_t kChromeRegClientStateMediumKey[] =
    L"Software\\BraveSoftware\\Update\\ClientStateMedium\\"
    L"{AFE6A462-C574-4B8A-AF43-4CC60DF4563B}";
#else
const wchar_t kChromeRegClientsKey[] =
    L"Software\\Google\\Update\\Clients\\"
    L"{8A69D345-D564-463c-AFF1-A69D9E530F96}";
const wchar_t kChromeRegClientStateKey[] =
    L"Software\\Google\\Update\\ClientState\\"
    L"{8A69D345-D564-463c-AFF1-A69D9E530F96}";
#endif

// Mirror the strategy used by GoogleUpdateSettings::GetBrand.
bool GetBrand(std::wstring* value) {
  const HKEY kRoots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
  for (HKEY root : kRoots) {
    if (base::win::RegKey(root, kChromeRegClientStateKey,
                          KEY_QUERY_VALUE | KEY_WOW64_32KEY)
            .ReadValue(google_update::kRegBrandField, value) == ERROR_SUCCESS) {
      return true;
    }
  }
  return false;
}

}  // namespace gcapi_internals
