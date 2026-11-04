// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/platform_mime_util.h"

#import <Foundation/Foundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <string>

#include "base/apple/bridging.h"
#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/notreached.h"
#include "base/strings/sys_string_conversions.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_IOS)
#include <MobileCoreServices/MobileCoreServices.h>
#else
#include <CoreServices/CoreServices.h>
#endif  // BUILDFLAG(IS_IOS)

namespace net {

#if BUILDFLAG(IS_MAC)
// FlyWeb B1d: UniformTypeIdentifiers (UTType, UTTagClass*) is macOS 11+. On
// macOS 10.14/10.15 use the CoreServices UTType* functions instead.
namespace {

bool HasUTTypeClass() {
  if (@available(macOS 11, *)) {
    return true;
  }
  return false;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
bool LegacyConvertTag(CFStringRef from_class,
                      const std::string& tag,
                      CFStringRef to_class,
                      std::string* result) {
  base::apple::ScopedCFTypeRef<CFStringRef> uti(
      UTTypeCreatePreferredIdentifierForTag(
          from_class, base::SysUTF8ToCFStringRef(tag).get(), nullptr));
  if (!uti || UTTypeIsDynamic(uti.get())) {
    return false;
  }
  base::apple::ScopedCFTypeRef<CFStringRef> value(
      UTTypeCopyPreferredTagWithClass(uti.get(), to_class));
  if (!value) {
    return false;
  }
  *result = base::SysCFStringRefToUTF8(value.get());
  return true;
}

void LegacyExtensionsForMimeType(
    std::string_view mime_type,
    std::unordered_set<base::FilePath::StringType>* extensions) {
  base::apple::ScopedCFTypeRef<CFArrayRef> utis(UTTypeCreateAllIdentifiersForTag(
      kUTTagClassMIMEType,
      base::SysUTF8ToCFStringRef(std::string(mime_type)).get(), nullptr));
  if (!utis) {
    return;
  }
  for (CFIndex i = 0; i < CFArrayGetCount(utis.get()); ++i) {
    CFStringRef uti =
        static_cast<CFStringRef>(CFArrayGetValueAtIndex(utis.get(), i));
    base::apple::ScopedCFTypeRef<CFArrayRef> tags(
        UTTypeCopyAllTagsWithClass(uti, kUTTagClassFilenameExtension));
    if (!tags) {
      continue;
    }
    for (CFIndex j = 0; j < CFArrayGetCount(tags.get()); ++j) {
      extensions->insert(base::SysCFStringRefToUTF8(
          static_cast<CFStringRef>(CFArrayGetValueAtIndex(tags.get(), j))));
    }
  }
}
#pragma clang diagnostic pop

}  // namespace
#endif  // BUILDFLAG(IS_MAC)

bool PlatformMimeUtil::GetPlatformMimeTypeFromExtension(
    const base::FilePath::StringType& ext,
    std::string* result) const {
  std::string ext_nodot = ext;
  if (ext_nodot.length() >= 1 && ext_nodot[0] == L'.') {
    ext_nodot.erase(ext_nodot.begin());
  }

#if BUILDFLAG(IS_MAC)
  if (!HasUTTypeClass()) {  // FlyWeb B1d
    return LegacyConvertTag(kUTTagClassFilenameExtension, ext_nodot,
                            kUTTagClassMIMEType, result);
  }
#endif  // BUILDFLAG(IS_MAC)
  UTType* uttype =
      [UTType typeWithFilenameExtension:base::SysUTF8ToNSString(ext_nodot)];
  // Dynamic UTTypes are made by the system in the event that there's a
  // non-identifiable mime type. For now, we should treat dynamic UTTypes as a
  // nonstandard format.
  if (uttype.dynamic || uttype.preferredMIMEType == nil) {
    return false;
  }
  *result = base::SysNSStringToUTF8(uttype.preferredMIMEType);
  return true;
}

bool PlatformMimeUtil::GetPlatformPreferredExtensionForMimeType(
    std::string_view mime_type,
    base::FilePath::StringType* ext) const {
#if BUILDFLAG(IS_MAC)
  if (!HasUTTypeClass()) {  // FlyWeb B1d
    return LegacyConvertTag(kUTTagClassMIMEType, std::string(mime_type),
                            kUTTagClassFilenameExtension, ext);
  }
#endif  // BUILDFLAG(IS_MAC)
  UTType* uttype = [UTType typeWithMIMEType:base::SysUTF8ToNSString(mime_type)];
  if (uttype.dynamic || uttype.preferredFilenameExtension == nil) {
    return false;
  }
  *ext = base::SysNSStringToUTF8(uttype.preferredFilenameExtension);
  return true;
}

void PlatformMimeUtil::GetPlatformExtensionsForMimeType(
    std::string_view mime_type,
    std::unordered_set<base::FilePath::StringType>* extensions) const {
#if BUILDFLAG(IS_MAC)
  if (!HasUTTypeClass()) {  // FlyWeb B1d
    LegacyExtensionsForMimeType(mime_type, extensions);
    return;
  }
#endif  // BUILDFLAG(IS_MAC)
  NSArray<UTType*>* types =
      [UTType typesWithTag:base::SysUTF8ToNSString(mime_type)
                  tagClass:UTTagClassMIMEType
          conformingToType:nil];
  bool extensions_found = false;
  if (types) {
    for (UTType* type in types) {
      if (!type || type.preferredFilenameExtension == nil) {
        continue;
      }
      extensions_found = true;
      NSArray<NSString*>* extensions_list =
          type.tags[UTTagClassFilenameExtension];
      for (NSString* extension in extensions_list) {
        extensions->insert(base::SysNSStringToUTF8(extension));
      }
    }
  }

  if (extensions_found) {
    return;
  }

  base::FilePath::StringType ext;
  if (GetPlatformPreferredExtensionForMimeType(mime_type, &ext)) {
    extensions->insert(ext);
  }
}

}  // namespace net
