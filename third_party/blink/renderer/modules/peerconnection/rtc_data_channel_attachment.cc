// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/peerconnection/rtc_data_channel_attachment.h"

namespace blink {

// FlyWeb: a unique address, not nullptr (crbug 511006880, f66bd3243b).
const void* const RTCDataChannelAttachment::kAttachmentKey =
    &RTCDataChannelAttachment::kAttachmentKey;

}  // namespace blink
