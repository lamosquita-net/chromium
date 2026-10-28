// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/clipboard/clipboard_item.h"

#include "base/containers/span.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/mojom/clipboard/clipboard.mojom-blink.h"
#include "third_party/blink/renderer/bindings/core/v8/script_function.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_blob.h"
#include "third_party/blink/renderer/core/clipboard/clipboard_mime_types.h"
#include "third_party/blink/renderer/core/fileapi/blob.h"
#include "third_party/blink/renderer/core/dom/dom_exception.h"
#include "third_party/blink/renderer/modules/clipboard/clipboard.h"
#include "third_party/blink/renderer/platform/bindings/script_state.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"
#include "ui/base/clipboard/clipboard_constants.h"

namespace blink {

namespace {

// FlyWeb level 133 (d72d71316f2): getType() always gives a Blob, but the
// representation may be a promise to a DOMString; the string becomes a Blob of
// the requested type. Written with ScriptFunction::Callable because the 128
// has no ThenCallable.
class ClipboardItemDataToBlobFunction final : public ScriptFunction::Callable {
 public:
  enum class ResolveType { kFulfill, kReject };

  ClipboardItemDataToBlobFunction(ScriptPromiseResolver<Blob>* resolver,
                                  const String& mime_type,
                                  ResolveType resolve_type)
      : resolver_(resolver),
        mime_type_(mime_type),
        resolve_type_(resolve_type) {}

  void Trace(Visitor* visitor) const final {
    ScriptFunction::Callable::Trace(visitor);
    visitor->Trace(resolver_);
  }

  ScriptValue Call(ScriptState* script_state, ScriptValue value) final {
    if (resolve_type_ == ResolveType::kReject) {
      resolver_->Reject(value);
      return ScriptValue();
    }
    v8::Isolate* isolate = script_state->GetIsolate();
    v8::Local<v8::Value> v8_value = value.V8Value();
    if (v8_value->IsString()) {
      std::string utf8 =
          ToCoreString(isolate, v8_value.As<v8::String>()).Utf8();
      resolver_->Resolve(
          Blob::Create(base::as_bytes(base::make_span(utf8)), mime_type_));
    } else if (Blob* blob = V8Blob::ToWrappable(isolate, v8_value)) {
      resolver_->Resolve(blob);
    } else {
      resolver_->RejectWithTypeError(
          "The ClipboardItem data is not a Blob or a string.");
    }
    return ScriptValue();
  }

 private:
  Member<ScriptPromiseResolver<Blob>> resolver_;
  String mime_type_;
  ResolveType resolve_type_;
};

}  // namespace

// static
ClipboardItem* ClipboardItem::Create(
    const HeapVector<std::pair<String, ScriptPromiseUntyped>>& representations,
    ExceptionState& exception_state) {
  // Check that incoming dictionary isn't empty. If it is, it's possible that
  // Javascript bindings implicitly converted an Object (like a
  // ScriptPromiseUntyped) into {}, an empty dictionary.
  if (!representations.size()) {
    exception_state.ThrowTypeError("Empty dictionary argument");
    return nullptr;
  }
  return MakeGarbageCollected<ClipboardItem>(representations);
}

ClipboardItem::ClipboardItem(
    const HeapVector<std::pair<String, ScriptPromiseUntyped>>&
        representations) {
  for (const auto& representation : representations) {
    String web_custom_format =
        Clipboard::ParseWebCustomFormat(representation.first);
    if (web_custom_format.empty()) {
      // Any arbitrary type can be added to ClipboardItem, but there may not be
      // any read/write support for that type.
      representations_.push_back(representation);
    } else {
      // Types with "web " prefix are special, so we do some level of MIME type
      // parsing here to get a valid web custom format type.
      // We want to ensure that the string after removing the "web " prefix is
      // a valid MIME type.
      // e.g. "web text/html" is a web custom MIME type & "text/html" is a
      // well-known MIME type. Removing the "web " prefix makes it hard to
      // differentiate between the two.
      String web_custom_format_string =
          String::Format("%s%s", ui::kWebClipboardFormatPrefix,
                         web_custom_format.Utf8().c_str());
      representations_.emplace_back(web_custom_format_string,
                                    representation.second);
      custom_format_types_.push_back(web_custom_format_string);
    }
  }
}

Vector<String> ClipboardItem::types() const {
  Vector<String> types;
  types.ReserveInitialCapacity(representations_.size());
  for (const auto& item : representations_) {
    types.push_back(item.first);
  }
  return types;
}

ScriptPromiseUntyped ClipboardItem::getType(
    ScriptState* script_state,
    const String& type,
    ExceptionState& exception_state) const {
  for (const auto& item : representations_) {
    if (type == item.first) {
      auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<Blob>>(
          script_state, exception_state.GetContext());
      ScriptPromiseUntyped promise = resolver->Promise();
      ScriptPromiseUntyped data = item.second;
      data.Then(
          MakeGarbageCollected<ScriptFunction>(
              script_state,
              MakeGarbageCollected<ClipboardItemDataToBlobFunction>(
                  resolver, type,
                  ClipboardItemDataToBlobFunction::ResolveType::kFulfill)),
          MakeGarbageCollected<ScriptFunction>(
              script_state,
              MakeGarbageCollected<ClipboardItemDataToBlobFunction>(
                  resolver, type,
                  ClipboardItemDataToBlobFunction::ResolveType::kReject)));
      return promise;
    }
  }

  exception_state.ThrowDOMException(DOMExceptionCode::kNotFoundError,
                                    "The type was not found");
  return ScriptPromiseUntyped();
}

// static
bool ClipboardItem::supports(const String& type) {
  if (type.length() >= mojom::blink::ClipboardHost::kMaxFormatSize) {
    return false;
  }

  if (!Clipboard::ParseWebCustomFormat(type).empty()) {
    return true;
  }

  if (type == kMimeTypeImageSvg) {
    return RuntimeEnabledFeatures::ClipboardSvgEnabled();
  }

  // TODO(https://crbug.com/1029857): Add support for other types.
  return type == kMimeTypeImagePng || type == kMimeTypeTextPlain ||
         type == kMimeTypeTextHTML;
}

void ClipboardItem::Trace(Visitor* visitor) const {
  visitor->Trace(representations_);
  ScriptWrappable::Trace(visitor);
}

}  // namespace blink
