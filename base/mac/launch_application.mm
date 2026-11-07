// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "base/mac/launch_application.h"

#include <variant>

#include "base/apple/bridging.h"
#include "base/apple/foundation_util.h"
#include "base/command_line.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/mac/launch_services_spi.h"
#include "base/mac/mac_util.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/sys_string_conversions.h"
#include "base/types/expected.h"

namespace base::mac {

namespace {

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class LaunchResult {
  kSuccess = 0,
  kSuccessDespiteError = 1,
  kFailure = 2,
  kMaxValue = kFailure,
};

NSArray* CommandLineArgsToArgsArray(const CommandLineArgs& command_line_args) {
  if (const CommandLine* command_line =
          std::get_if<CommandLine>(&command_line_args)) {
    const auto& argv = command_line->argv();
    size_t argc = argv.size();
    DCHECK_GT(argc, 0lu);

    NSMutableArray* args_array = [NSMutableArray arrayWithCapacity:argc - 1];
    // NSWorkspace automatically adds the binary path as the first argument and
    // thus it should not be included in the list.
    for (size_t i = 1; i < argc; ++i) {
      [args_array addObject:base::SysUTF8ToNSString(argv[i])];
    }

    return args_array;
  }

  if (const std::vector<std::string>* string_vector =
          std::get_if<std::vector<std::string>>(&command_line_args)) {
    NSMutableArray* args_array =
        [NSMutableArray arrayWithCapacity:string_vector->size()];
    for (const auto& arg : *string_vector) {
      [args_array addObject:base::SysUTF8ToNSString(arg)];
    }

    return args_array;
  }

  return @[];
}

API_AVAILABLE(macos(10.15))  // FlyWeb B1d
NSWorkspaceOpenConfiguration* GetOpenConfiguration(
    LaunchApplicationOptions options,
    const CommandLineArgs& command_line_args) {
  NSWorkspaceOpenConfiguration* config =
      [NSWorkspaceOpenConfiguration configuration];

  config.arguments = CommandLineArgsToArgsArray(command_line_args);

  config.activates = options.activate;
  config.createsNewApplicationInstance = options.create_new_instance;
  config.promptsUserIfNeeded = options.prompt_user_if_needed;

  if (options.hidden_in_background) {
    config.addsToRecentItems = NO;
    config.hides = YES;
    if (&_kLSOpenOptionBackgroundLaunchKey != nullptr) {  // FlyWeb B1b
      config._additionalLSOpenOptions = @{
        apple::CFToNSPtrCast(_kLSOpenOptionBackgroundLaunchKey) : @YES,
      };
    }
  }

  return config;
}

void LogResultAndInvokeCallback(const base::FilePath& app_bundle_path,
                                bool create_new_instance,
                                LaunchApplicationCallback callback,
                                NSRunningApplication* app,
                                NSError* error) {
  UmaHistogramEnumeration(
      "Mac.LaunchApplicationResult",
      app ? LaunchResult::kSuccess : LaunchResult::kFailure);

  if (error) {
    LOG(ERROR) << base::SysNSStringToUTF8(error.localizedDescription);
    std::move(callback).Run(nil, error);
  } else {
    std::move(callback).Run(app, nil);
  }
}

}  // namespace

void LaunchApplication(const base::FilePath& app_bundle_path,
                       const CommandLineArgs& command_line_args,
                       const std::vector<std::string>& url_specs,
                       LaunchApplicationOptions options,
                       LaunchApplicationCallback callback) {
  __block LaunchApplicationCallback callback_block_access =
      base::BindOnce(&LogResultAndInvokeCallback, app_bundle_path,
                     options.create_new_instance, std::move(callback));

  NSURL* bundle_url = apple::FilePathToNSURL(app_bundle_path);
  if (!bundle_url) {
    dispatch_async(dispatch_get_main_queue(), ^{
      std::move(callback_block_access)
          .Run(nil, [NSError errorWithDomain:NSCocoaErrorDomain
                                        code:NSFileNoSuchFileError
                                    userInfo:nil]);
    });
    return;
  }

  NSMutableArray* ns_urls = nil;
  if (!url_specs.empty()) {
    ns_urls = [NSMutableArray arrayWithCapacity:url_specs.size()];
    for (const auto& url_spec : url_specs) {
      [ns_urls
          addObject:[NSURL URLWithString:base::SysUTF8ToNSString(url_spec)]];
    }
  }

  void (^action_block)(NSRunningApplication*, NSError*) =
      ^void(NSRunningApplication* app, NSError* error) {
        dispatch_async(dispatch_get_main_queue(), ^{
          std::move(callback_block_access).Run(app, error);
        });
      };

  // FlyWeb B1d: NSWorkspaceOpenConfiguration and the configuration: launch
  // methods are macOS 10.15+; use the dictionary-based API on macOS 10.14.
  // The 10.15+ path is inside the @available block so that
  // -Wunguarded-availability sees it guarded.
  if (@available(macOS 10.15, *)) {
    NSWorkspaceOpenConfiguration* configuration =
        GetOpenConfiguration(options, command_line_args);

    if (ns_urls) {
      [NSWorkspace.sharedWorkspace openURLs:ns_urls
                       withApplicationAtURL:bundle_url
                              configuration:configuration
                          completionHandler:action_block];
    } else {
      [NSWorkspace.sharedWorkspace openApplicationAtURL:bundle_url
                                          configuration:configuration
                                      completionHandler:action_block];
    }
  } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    NSWorkspaceLaunchOptions launch_options = NSWorkspaceLaunchDefault;
    if (!options.activate) {
      launch_options |= NSWorkspaceLaunchWithoutActivation;
    }
    if (options.create_new_instance) {
      launch_options |= NSWorkspaceLaunchNewInstance;
    }
    // FlyWeb B1d: as config.promptsUserIfNeeded on 10.15+ (and as in 116).
    if (options.prompt_user_if_needed) {
      launch_options |= NSWorkspaceLaunchWithErrorPresentation;
    }
    if (options.hidden_in_background) {
      launch_options |= NSWorkspaceLaunchAndHide |
                        NSWorkspaceLaunchWithoutAddingToRecents;
    }
    NSDictionary* legacy_config = @{
      NSWorkspaceLaunchConfigurationArguments :
          CommandLineArgsToArgsArray(command_line_args)
    };
    NSError* error = nil;
    NSRunningApplication* app = nil;
    if (ns_urls) {
      app = [NSWorkspace.sharedWorkspace openURLs:ns_urls
                             withApplicationAtURL:bundle_url
                                          options:launch_options
                                    configuration:legacy_config
                                            error:&error];
    } else {
      app = [NSWorkspace.sharedWorkspace launchApplicationAtURL:bundle_url
                                                        options:launch_options
                                                  configuration:legacy_config
                                                          error:&error];
    }
#pragma clang diagnostic pop
    action_block(app, error);
  }
}

}  // namespace base::mac
