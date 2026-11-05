// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/shape_detection/barcode_detection_impl_mac_vision_api.h"

#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"

namespace shape_detection {

namespace {

class VisionAPI : public VisionAPIInterface {
 public:
  VisionAPI() = default;

  ~VisionAPI() override = default;

  NSArray<VNBarcodeSymbology>* GetSupportedSymbologies() const override {
    VNDetectBarcodesRequest* barcodes_request =
        [[VNDetectBarcodesRequest alloc] init];
    NSError* error = nil;
    NSArray<VNBarcodeSymbology>* symbologies = nil;
    if (@available(macOS 12.0, *)) {
      symbologies =
          [barcodes_request supportedSymbologiesAndReturnError:&error];
    } else {
      // FlyWeb B1d: -supportedSymbologiesAndReturnError: is macOS 12+.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      symbologies = VNDetectBarcodesRequest.supportedSymbologies;
#pragma clang diagnostic pop
    }
    if (error) {
      DLOG(ERROR) << base::SysNSStringToUTF8(error.localizedDescription);
    }
    return symbologies;
  }
};

}  // namespace

// static
std::unique_ptr<VisionAPIInterface> VisionAPIInterface::Create() {
  return std::make_unique<VisionAPI>();
}

}  // namespace shape_detection
