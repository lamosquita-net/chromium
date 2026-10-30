// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_VIEW_TRANSITION_VIEW_TRANSITION_MATCH_ELEMENT_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_VIEW_TRANSITION_VIEW_TRANSITION_MATCH_ELEMENT_H_

#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"

namespace blink {

// FlyWeb: the 128 base keeps view-transition-name as a ScopedCSSName (the
// StyleViewTransitionName of Chromium 137 is 13x infrastructure), so
// `view-transition-name: match-element` is stored with this name. The parser
// consumes match-element as a keyword, so no author custom-ident can produce
// it, and ViewTransitionStyleTracker replaces it with a per-element name.
inline const AtomicString& ViewTransitionMatchElementName() {
  DEFINE_STATIC_LOCAL(const AtomicString, name, ("match-element"));
  return name;
}

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_VIEW_TRANSITION_VIEW_TRANSITION_MATCH_ELEMENT_H_
