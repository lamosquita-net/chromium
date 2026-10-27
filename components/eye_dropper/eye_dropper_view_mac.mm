// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// FlyWeb (FM.42 b2): the Mac part of the generic eye dropper, from 116
// (chrome/browser/ui/views/eye_dropper/eye_dropper_view_mac.mm). Used on macOS
// 10.14, which has no NSColorSampler.

#include "components/eye_dropper/eye_dropper_view.h"

#include <Carbon/Carbon.h>  // For keycode names in Carbon's Event.h.
#import <Cocoa/Cocoa.h>

#include <memory>

#include "ui/views/widget/widget.h"

namespace eye_dropper {

struct EyeDropperView::PreEventDispatchHandler::ObjCStorage {
  id __strong click_event_tap = nil;
  id __strong notification_observer = nil;
};

EyeDropperView::PreEventDispatchHandler::PreEventDispatchHandler(
    EyeDropperView* view,
    gfx::NativeView parent)
    : view_(view), objc_storage_(std::make_unique<ObjCStorage>()) {
  // Ensure that this handler is called before color popup handler.
  objc_storage_->click_event_tap = [NSEvent
      addLocalMonitorForEventsMatchingMask:NSEventMaskAny
                                   handler:^NSEvent*(NSEvent* event) {
                                     NSEventType eventType = [event type];
                                     if (eventType ==
                                             NSEventTypeLeftMouseDown ||
                                         eventType ==
                                             NSEventTypeRightMouseDown) {
                                       view_->OnColorSelected();
                                       return nil;
                                     } else if (eventType ==
                                                    NSEventTypeKeyDown &&
                                                [event keyCode] == kVK_Escape) {
                                       view_->OnColorSelectionCanceled();
                                       return nil;
                                     }

                                     return event;
                                   }];

  // Needed because the local event monitor doesn't see the click on the
  // menubar.
  objc_storage_->notification_observer = [NSNotificationCenter.defaultCenter
      addObserverForName:NSMenuDidBeginTrackingNotification
                  object:NSApp.mainMenu
                   queue:NSOperationQueue.mainQueue
              usingBlock:^(NSNotification* note) {
                view_->OnColorSelected();
              }];
}

EyeDropperView::PreEventDispatchHandler::~PreEventDispatchHandler() {
  if (objc_storage_->click_event_tap) {
    [NSEvent removeMonitor:objc_storage_->click_event_tap];
    objc_storage_->click_event_tap = nil;
  }

  if (objc_storage_->notification_observer) {
    [NSNotificationCenter.defaultCenter
        removeObserver:objc_storage_->notification_observer];
    objc_storage_->notification_observer = nil;
  }
}

void EyeDropperView::PreEventDispatchHandler::OnMouseEvent(
    ui::MouseEvent* event) {
  // The event monitor already provides a handler.
}

void EyeDropperView::PreEventDispatchHandler::OnGestureEvent(
    ui::GestureEvent* event) {}

void EyeDropperView::PreEventDispatchHandler::OnTouchEvent(
    ui::TouchEvent* event) {}

void EyeDropperView::CaptureInput() {
  // There is no need to capture input on Mac. Move the window to the front
  // within the popup level instead, since the eye dropper can be opened from
  // the color picker (116 MoveViewToFront()).
  NSWindow* window = GetWidget()->GetNativeWindow().GetNativeNSWindow();
  [window setLevel:NSPopUpMenuWindowLevel];
  [window makeKeyAndOrderFront:nil];
}

void EyeDropperView::HideCursor() {
  [NSCursor hide];
}

void EyeDropperView::ShowCursor() {
  [NSCursor unhide];
}

gfx::Size EyeDropperView::GetSize() const {
  return gfx::Size(90, 90);
}

float EyeDropperView::GetDiameter() const {
  return 90;
}

}  // namespace eye_dropper
