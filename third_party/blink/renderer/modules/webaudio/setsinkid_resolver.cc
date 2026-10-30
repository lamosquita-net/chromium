// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/webaudio/setsinkid_resolver.h"

#include "third_party/blink/renderer/bindings/core/v8/v8_throw_dom_exception.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_union_audiosinkinfo_string.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/modules/webaudio/audio_context.h"
#include "third_party/blink/renderer/modules/webaudio/realtime_audio_destination_node.h"
#include "third_party/blink/renderer/platform/audio/audio_utilities.h"

namespace blink {

SetSinkIdResolver::SetSinkIdResolver(
    ScriptState* script_state,
    AudioContext& audio_context,
    const V8UnionAudioSinkOptionsOrString& sink_id)
    : audio_context_(audio_context),
      resolver_(MakeGarbageCollected<ScriptPromiseResolver<IDLUndefined>>(
          script_state)) {
  DCHECK(IsMainThread());
  // Currently the only available AudioSinkOptions is a type of a silent sink,
  // which can be specified by an empty descriptor constructor.
  auto& frame_token = To<LocalDOMWindow>(audio_context_->GetExecutionContext())
                          ->GetLocalFrameToken();
  if (sink_id.GetContentType() ==
      V8UnionAudioSinkOptionsOrString::ContentType::kAudioSinkOptions) {
    sink_descriptor_ = WebAudioSinkDescriptor(frame_token);
  } else {
    sink_descriptor_ =
        WebAudioSinkDescriptor(sink_id.GetAsString(), frame_token);
  }

  TRACE_EVENT1("webaudio", "SetSinkIdResolver::SetSinkIdResolver",
               "sink_id (after setting sink_descriptor_)",
               audio_utilities::GetSinkIdForTracing(sink_descriptor_));
}

void SetSinkIdResolver::Start() {
  TRACE_EVENT1("webaudio", "SetSinkIdResolver::Start", "sink_id",
               audio_utilities::GetSinkIdForTracing(sink_descriptor_));
  DCHECK(IsMainThread());

  if (!resolver_ || !resolver_->GetExecutionContext() || !audio_context_ ||
      audio_context_->IsContextCleared()) {
    // No point in rejecting promise, as it will bail out upon detached
    // context anyway.
    return;
  }

  auto set_sink_id_completion_callback = WTF::BindOnce(
      &SetSinkIdResolver::OnSetSinkIdComplete, WrapPersistent(this));

  // Refer to
  // https://webaudio.github.io/web-audio-api/#validating-sink-identifier for
  // sink_id/sink_descriptor validation steps.
  if (sink_descriptor_ == audio_context_->GetSinkDescriptor()) {
    std::move(set_sink_id_completion_callback)
        .Run(media::OutputDeviceStatus::OUTPUT_DEVICE_STATUS_OK);
  } else if (!audio_context_->IsValidSinkDescriptor(sink_descriptor_)) {
    std::move(set_sink_id_completion_callback)
        .Run(media::OutputDeviceStatus::OUTPUT_DEVICE_STATUS_ERROR_NOT_FOUND);
  } else {
    auto* audio_destination = audio_context_->destination();

    // A sanity check to make sure we have valid audio_destination node from
    // `audio_context_`.
    if (!audio_destination) {
      std::move(set_sink_id_completion_callback)
          .Run(media::OutputDeviceStatus::OUTPUT_DEVICE_STATUS_ERROR_INTERNAL);
    } else {
      audio_context_->NotifySetSinkIdBegins();
      auto set_sink_descriptor_callback = WTF::BindOnce(
          &RealtimeAudioDestinationNode::SetSinkDescriptor,
          WrapWeakPersistent(
              static_cast<RealtimeAudioDestinationNode*>(audio_destination)),
          sink_descriptor_, std::move(set_sink_id_completion_callback));
      audio_context_->GetExecutionContext()
          ->GetTaskRunner(TaskType::kInternalMediaRealTime)
          ->PostTask(FROM_HERE, std::move(set_sink_descriptor_callback));
    }
  }
}

void SetSinkIdResolver::Resolve() {
  DCHECK(IsMainThread());
  DCHECK(resolver_);
  resolver_->Resolve();
  resolver_ = nullptr;
}

void SetSinkIdResolver::Reject(DOMException* exception) {
  DCHECK(IsMainThread());
  DCHECK(resolver_);
  resolver_->Reject(exception);
  resolver_ = nullptr;
}

void SetSinkIdResolver::Reject(v8::Local<v8::Value> value) {
  DCHECK(IsMainThread());
  DCHECK(resolver_);
  resolver_->Reject(value);
  resolver_ = nullptr;
}

ScriptPromise<IDLUndefined> SetSinkIdResolver::GetPromise() {
  DCHECK(IsMainThread());
  DCHECK(resolver_);
  return resolver_->Promise();
}

void SetSinkIdResolver::HandleOutputDeviceStatus(
    media::OutputDeviceStatus status) {
  ScriptState* script_state = resolver_->GetScriptState();
  ScriptState::Scope scope(script_state);

  switch (status) {
    case media::OutputDeviceStatus::OUTPUT_DEVICE_STATUS_OK:
      // Update AudioContext's sink ID and fire the 'onsinkchange' event
      NotifySetSinkIdIsDone();
      // FlyWeb: 'sinkchange' is dispatched synchronously; a close() from its
      // handler has already rejected this resolver.
      if (resolver_) {
        Resolve();
      }
      return;
    case media::OutputDeviceStatus::OUTPUT_DEVICE_STATUS_ERROR_NOT_FOUND:
      Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state->GetIsolate(), DOMExceptionCode::kNotFoundError,
          "AudioContext.setSinkId(): failed: the device " +
              String(sink_descriptor_.SinkId()) + " is not found."));
      return;
    case media::OutputDeviceStatus::OUTPUT_DEVICE_STATUS_ERROR_NOT_AUTHORIZED:
      Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state->GetIsolate(), DOMExceptionCode::kNotAllowedError,
          "AudioContext.setSinkId() failed: access to the device " +
              String(sink_descriptor_.SinkId()) + " is not permitted."));
      return;
    case media::OutputDeviceStatus::OUTPUT_DEVICE_STATUS_ERROR_TIMED_OUT:
      Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state->GetIsolate(), DOMExceptionCode::kTimeoutError,
          "AudioContext.setSinkId() failed: the request for device " +
              String(sink_descriptor_.SinkId()) + " is timed out."));
      return;
    case media::OutputDeviceStatus::OUTPUT_DEVICE_STATUS_ERROR_INTERNAL:
      Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state->GetIsolate(), DOMExceptionCode::kInvalidStateError,
          "AudioContext.setSinkId() failed: the device " +
              String(sink_descriptor_.SinkId()) + " is not available."));
      return;
  }
  NOTREACHED_IN_MIGRATION();
  resolver_ = nullptr;
}

void SetSinkIdResolver::OnSetSinkIdComplete(media::OutputDeviceStatus status) {
  TRACE_EVENT1("webaudio", "SetSinkIdResolver::OnSetSinkIdComplete", "sink_id",
               audio_utilities::GetSinkIdForTracing(sink_descriptor_));
  DCHECK(IsMainThread());

  // FlyWeb: null once the AudioContext was closed (upstream 511088344); the
  // resolver queue was already cleared, so pop_front() would underflow it.
  if (!resolver_ || !audio_context_) {
    return;
  }

  auto* excecution_context = resolver_->GetExecutionContext();
  if (!excecution_context || excecution_context->IsContextDestroyed()) {
    return;
  }

  HandleOutputDeviceStatus(status);

  // FlyWeb: the same close() clears the queue (and the context may be gone);
  // only pop this resolver.
  if (!audio_context_) {
    return;
  }
  auto& resolvers = audio_context_->GetSetSinkIdResolver();
  if (resolvers.empty() || resolvers.front().Get() != this) {
    return;
  }

  resolvers.pop_front();

  if (!resolvers.empty()) {
    // Prevent potential stack overflow under heavy load by scheduling the next
    // resolver start asynchronously instead of invoking it directly.
    auto next_start_task = WTF::BindOnce(
        &SetSinkIdResolver::Start, WrapWeakPersistent(resolvers.front().Get()));
    audio_context_->GetExecutionContext()
        ->GetTaskRunner(TaskType::kInternalMediaRealTime)
        ->PostTask(FROM_HERE, std::move(next_start_task));
  }
}

void SetSinkIdResolver::NotifySetSinkIdIsDone() {
  DCHECK(IsMainThread());

  if (!audio_context_ || audio_context_->IsContextCleared()) {
    return;
  }

  audio_context_->NotifySetSinkIdIsDone(sink_descriptor_);
}

void SetSinkIdResolver::Trace(Visitor* visitor) const {
  visitor->Trace(audio_context_);
  visitor->Trace(resolver_);
}

}  // namespace blink
