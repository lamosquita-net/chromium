// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/file_utilities_host_impl.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/files/file_util.h"
#include "build/build_config.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "mojo/public/cpp/bindings/message.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"

#if BUILDFLAG(IS_MAC)
#include <fcntl.h>
#include <sys/stat.h>

#include "base/mac/mac_util.h"
#endif

namespace content {

FileUtilitiesHostImpl::FileUtilitiesHostImpl(int process_id)
    : process_id_(process_id) {}

FileUtilitiesHostImpl::~FileUtilitiesHostImpl() = default;

void FileUtilitiesHostImpl::Create(
    int process_id,
    mojo::PendingReceiver<blink::mojom::FileUtilitiesHost> receiver) {
  mojo::MakeSelfOwnedReceiver(
      std::make_unique<FileUtilitiesHostImpl>(process_id), std::move(receiver));
}

void FileUtilitiesHostImpl::GetFileInfo(const base::FilePath& path,
                                        GetFileInfoCallback callback) {
  // Get file metadata only when the child process has been granted
  // permission to read the file.
  auto* security_policy = ChildProcessSecurityPolicyImpl::GetInstance();
  if (!security_policy->CanReadFile(process_id_, path)) {
    std::move(callback).Run(std::nullopt);
    return;
  }

  base::File::Info info;
  if (base::GetFileInfo(path, &info)) {
    std::move(callback).Run(info);
  } else {
    std::move(callback).Run(std::nullopt);
  }
}

#if BUILDFLAG(IS_MAC)
// FlyWeb (FM.42 c3, from 116).
void FileUtilitiesHostImpl::SetLength(base::File file,
                                      const int64_t length,
                                      SetLengthCallback callback) {
  if (base::mac::MacOSVersion() >= 10'15'00) {
    mojo::ReportBadMessage("SetLength() disabled on this OS.");
    // No error message is specified as the ReportBadMessage() call should close
    // the pipe and kill the renderer.
    std::move(callback).Run(std::move(file), false);
    return;
  }
  // FlyWeb (FS.26): a compromised renderer can pass any descriptor it holds,
  // and on 10.14 its own sandbox would not let it ftruncate() any of them.
  // Only act on what OPFS hands out: a valid, regular file open for writing
  // and not in append mode (stderr redirected to a log file, for example).
  if (!file.IsValid() || length < 0) {
    mojo::ReportBadMessage("SetLength() with an invalid file or length.");
    std::move(callback).Run(std::move(file), false);
    return;
  }
  const int fd = file.GetPlatformFile();
  struct stat st;
  const int flags = fcntl(fd, F_GETFL);
  if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || flags == -1 ||
      (flags & O_ACCMODE) == O_RDONLY || (flags & O_APPEND)) {
    std::move(callback).Run(std::move(file), false);
    return;
  }
  bool result = file.SetLength(length);
  std::move(callback).Run(std::move(file), result);
}
#endif  // BUILDFLAG(IS_MAC)

}  // namespace content
