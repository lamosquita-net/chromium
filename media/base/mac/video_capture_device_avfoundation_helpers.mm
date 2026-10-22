// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/mac/video_capture_device_avfoundation_helpers.h"

#include "base/feature_list.h"
#include "build/build_config.h"

namespace media {

#if BUILDFLAG(IS_MAC)
BASE_FEATURE(kUseAVCaptureDeviceTypeExternal,
             "UseAVCaptureDeviceTypeExternal",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_MAC)

NSArray<AVCaptureDevice*>* GetVideoCaptureDevices() {
#if BUILDFLAG(IS_MAC)
  // FlyWeb (LOCAL-8): the device types and AVCaptureDeviceDiscoverySession are
  // macOS 10.15+ (null weak constants in an array literal abort on 10.14). As in
  // 116, 10.14 uses +[AVCaptureDevice devices].
  if (@available(macOS 10.15, *)) {
  } else {
    return [AVCaptureDevice devices];
  }
#endif  // BUILDFLAG(IS_MAC)
  // Camera device types available on all apple platforms.
  NSArray* captureDeviceTypes = @[ AVCaptureDeviceTypeBuiltInWideAngleCamera ];

#if BUILDFLAG(IS_MAC)
  // MacOS has an additional 'external' device type we want to include.
  // AVCaptureDeviceTypeExternal since 14.0, AVCaptureDeviceTypeExternalUnknown
  // before. See crbug.com/1484830.
  if (@available(macOS 14.0, *)) {
    if (base::FeatureList::IsEnabled(kUseAVCaptureDeviceTypeExternal)) {
      captureDeviceTypes =
          [captureDeviceTypes arrayByAddingObject:AVCaptureDeviceTypeExternal];
    } else {
      // @available needs to be alone in an if statement, so we need to
      // duplicate the else case here.
      captureDeviceTypes = [captureDeviceTypes
          arrayByAddingObject:AVCaptureDeviceTypeExternalUnknown];
    }
  } else {
    captureDeviceTypes = [captureDeviceTypes
        arrayByAddingObject:AVCaptureDeviceTypeExternalUnknown];
  }
#endif  // BUILDFLAG(IS_MAC)

  AVCaptureDeviceDiscoverySession* deviceDiscoverySession =
      [AVCaptureDeviceDiscoverySession
          discoverySessionWithDeviceTypes:captureDeviceTypes
                                mediaType:AVMediaTypeVideo
                                 position:AVCaptureDevicePositionUnspecified];
  return deviceDiscoverySession.devices;
}

}  // namespace media
