// Copyright 2026 The surround_encoder authors. MIT license.
//
// macOS backend: system playback audio via ScreenCaptureKit (macOS 13+),
// microphone via AVAudioEngine. Both deliver interleaved float32 PCM into the
// shared C++ encoder pipeline.

#import "Messages.g.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <mutex>
#include <string>
#include <vector>

#import "../../src/common/capture_source.h"
#import "../../src/common/encoder_pipeline.h"

@class SystemAudioHelper;
@class MicAudioHelper;

namespace surround_encoder {

// ---------------------------------------------------------------------------

// CFString -> std::string (CFStringGetCStringPtr may return null).
inline std::string CFStringToStd(CFStringRef ref) {
  if (ref == nullptr) {
    return std::string();
  }
  char buffer[256];
  if (CFStringGetCString(ref, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
    return std::string(buffer);
  }
  return std::string();
}

// C++ wrapper around the ScreenCaptureKit-based system audio source.
class MacSystemSource : public ICaptureSource {
 public:
  ~MacSystemSource() override { Stop(); }

  bool Start(AudioChunkCallback callback, std::string* error) override {
    if (@available(macOS 13.0, *)) {
      helper_ = [[SystemAudioHelper alloc] init];
      return [helper_ startWithChunkCallback:std::move(callback)
                                       error:error];
    }
    *error = "system audio capture requires macOS 13.0 or newer";
    return false;
  }

  void Stop() override {
    if (helper_ != nullptr) {
      [helper_ stop];
      helper_ = nullptr;
    }
  }

 private:
  SystemAudioHelper* helper_ = nullptr;
};

// C++ wrapper around the AVAudioEngine microphone tap.
class MacMicSource : public ICaptureSource {
 public:
  ~MacMicSource() override { Stop(); }

  bool Start(AudioChunkCallback callback, std::string* error) override {
    helper_ = [[MicAudioHelper alloc] init];
    return [helper_ startWithChunkCallback:std::move(callback)
                                deviceUid:device_uid_
                                    error:error];
  }

  void Stop() override {
    if (helper_ != nullptr) {
      [helper_ stop];
      helper_ = nullptr;
    }
  }

  void set_device_uid(const std::string& uid) { device_uid_ = uid; }

 private:
  MicAudioHelper* helper_ = nullptr;
  std::string device_uid_;
};

// ---------------------------------------------------------------------------

std::vector<DeviceInfo> ListCaptureDevices() {
  std::vector<DeviceInfo> devices;

  if (@available(macOS 13.0, *)) {
    DeviceInfo system;
    system.kind = SourceKind::kSystem;
    system.id = "system";
    system.name = "System playback audio (ScreenCaptureKit)";
    system.is_default = true;
    devices.push_back(std::move(system));
  }

  // Microphones: enumerate CoreAudio input-capable devices.
  AudioObjectPropertyAddress address = {
      kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMaster};
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0,
                                     nullptr, &size) != noErr || size == 0) {
    return devices;
  }
  const UInt32 count = size / sizeof(AudioDeviceID);
  std::vector<AudioDeviceID> ids(count);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0,
                                 nullptr, &size, ids.data()) != noErr) {
    return devices;
  }
  AudioDeviceID default_input = kAudioObjectUnknown;
  AudioObjectPropertyAddress default_address = {
      kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMaster};
  size = sizeof(default_input);
  AudioObjectGetPropertyData(kAudioObjectSystemObject, &default_address, 0,
                             nullptr, &size, &default_input);

  for (const AudioDeviceID id : ids) {
    AudioObjectPropertyAddress streams_address = {
        kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput,
        kAudioObjectPropertyElementMaster};
    UInt32 streams_size = 0;
    if (AudioObjectGetPropertyDataSize(id, &streams_address, 0, nullptr,
                                       &streams_size) != noErr ||
        streams_size == 0) {
      continue;  // no input streams -> not a microphone
    }

    DeviceInfo info;
    info.kind = SourceKind::kMicrophone;
    info.is_default = (id == default_input);

    AudioObjectPropertyAddress uid_address = {
        kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster};
    CFStringRef uid = nullptr;
    size = sizeof(uid);
    if (AudioObjectGetPropertyData(id, &uid_address, 0, nullptr, &size,
                                   &uid) == noErr && uid != nullptr) {
      info.id = CFStringToStd(uid);
      CFRelease(uid);
    }
    if (info.id.empty()) {
      continue;
    }

    AudioObjectPropertyAddress name_address = {
        kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster};
    CFStringRef name = nullptr;
    size = sizeof(name);
    if (AudioObjectGetPropertyData(id, &name_address, 0, nullptr, &size,
                                   &name) == noErr && name != nullptr) {
      info.name = CFStringToStd(name);
      CFRelease(name);
    }
    if (info.name.empty()) {
      info.name = info.id;
    }
    devices.push_back(std::move(info));
  }
  return devices;
}

std::unique_ptr<ICaptureSource> CreateCaptureSource(const SourceSpec& spec,
                                                    std::string* error) {
  if (spec.kind == SourceKind::kSystem) {
    return std::make_unique<MacSystemSource>();
  }
  auto source = std::make_unique<MacMicSource>();
  source->set_device_uid(spec.device_id);
  return source;
}

const char* PlatformName() { return "macOS"; }

}  // namespace surround_encoder

// ---------------------------------------------------------------------------
// System audio via ScreenCaptureKit (macOS 13+).

API_AVAILABLE(macos(13.0))
@interface SystemAudioHelper
    : NSObject <SCStreamOutput, SCStreamDelegate>
@property(nonatomic) surround_encoder::AudioChunkCallback chunk;
@property(nonatomic, strong) SCStream* stream;
@property(nonatomic) uint32_t sample_rate;
@property(nonatomic) uint32_t channels;
@end

@implementation SystemAudioHelper

- (BOOL)startWithChunkCallback:(surround_encoder::AudioChunkCallback)callback
                         error:(std::string*)error {
  self.chunk = std::move(callback);

  __block SCContentFilter* filter = nullptr;
  __block NSString* fetch_error = nullptr;
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  [SCShareableContent
      getShareableContentExcludingDesktopWindows:NO
                             onScreenWindowsOnly:YES
                               completionHandler:^(
                                   SCShareableContent* content,
                                   NSError* err) {
        if (err != nullptr) {
          fetch_error = err.localizedDescription;
        } else if (content.displays.count > 0) {
          filter = [[SCContentFilter alloc]
              initWithDisplay:content.displays.firstObject
              excludingWindows:@[]];
        }
        dispatch_semaphore_signal(done);
      }];
  dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
  if (filter == nullptr) {
    *error = fetch_error != nullptr
                 ? std::string(fetch_error.UTF8String)
                 : std::string("no displays available for capture");
    return false;
  }

  SCStreamConfiguration* config = [SCStreamConfiguration new];
  config.capturesAudio = YES;
  config.sampleRate = 48000;
  config.channelCount = 2;
  config.excludesCurrentProcessAudio = YES;
  config.queueDepth = 8;

  self.stream = [[SCStream alloc] initWithFilter:filter
                                   configuration:config
                                        delegate:self];

  NSError* add_error = nullptr;
  dispatch_queue_t queue =
      dispatch_queue_create("surround_encoder.sck", DISPATCH_QUEUE_SERIAL);
  if (![self.stream addStreamOutput:self
                               type:SCStreamOutputTypeAudio
                 sampleHandlerQueue:queue
                              error:&add_error]) {
    *error = add_error.localizedDescription != nullptr
                 ? std::string(add_error.localizedDescription.UTF8String)
                 : std::string("failed to attach audio output");
    self.stream = nullptr;
    return false;
  }

  __block BOOL started = NO;
  __block NSString* start_error = nullptr;
  dispatch_semaphore_t started_sem = dispatch_semaphore_create(0);
  [self.stream startCaptureWithCompletionHandler:^(NSError* err) {
    started = err == nullptr;
    if (err != nullptr) {
      start_error = err.localizedDescription;
    }
    dispatch_semaphore_signal(started_sem);
  }];
  dispatch_semaphore_wait(started_sem, DISPATCH_TIME_FOREVER);
  if (!started) {
    *error = start_error != nullptr ? std::string(start_error.UTF8String)
                                    : std::string("stream start failed");
    self.stream = nullptr;
    return false;
  }
  return YES;
}

- (void)stop {
  if (self.stream != nullptr) {
    [self.stream stopCaptureWithCompletionHandler:^(NSError* err){}];
    [self.stream removeStreamOutput:self type:SCStreamOutputTypeAudio];
    self.stream = nullptr;
  }
}

- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           ofMediaType:(SCFrameType)type {
  if (type != SCFrameTypeAudio ||
      CMSampleBufferGetNumSamples(sampleBuffer) == 0) {
    return;
  }
  const CMItemCount frames = CMSampleBufferGetNumSamples(sampleBuffer);
  if (frames == 0) {
    return;
  }

  const AudioStreamBasicDescription* asbd =
      CMAudioFormatDescriptionGetStreamBasicDescription(
          CMSampleBufferGetFormatDescription(sampleBuffer));
  if (asbd == nullptr) {
    return;
  }
  self.sample_rate = static_cast<uint32_t>(asbd->mSampleRate);
  self.channels = asbd->mChannelsPerFrame;

  AudioBufferList buffer_list;
  CMBlockBufferRef block = nullptr;
  OSStatus status = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
      sampleBuffer, nullptr, &buffer_list, sizeof(buffer_list), nullptr,
      nullptr, 0, &block);
  if (status != noErr) {
    return;
  }

  const uint32_t ch = self.channels;
  const uint32_t frames_u = static_cast<uint32_t>(frames);
  std::vector<float> interleaved;
  interleaved.reserve(static_cast<size_t>(frames_u) * ch);
  // SCK delivers non-interleaved float32.
  for (uint32_t i = 0; i < frames_u; ++i) {
    for (uint32_t c = 0; c < ch; ++c) {
      const AudioBuffer& buffer = buffer_list.mBuffers[c];
      const float* plane = reinterpret_cast<const float*>(buffer.mData);
      interleaved.push_back(plane[i]);
    }
  }
  CFRelease(block);

  surround_encoder::CaptureFormat format;
  format.sample_rate = self.sample_rate;
  format.channels = ch;
  self.chunk(interleaved.data(), frames_u, format);
}

@end

// ---------------------------------------------------------------------------
// Microphone via AVAudioEngine input tap.

@interface MicAudioHelper : NSObject
@property(nonatomic) surround_encoder::AudioChunkCallback chunk;
@property(nonatomic, strong) AVAudioEngine* engine;
@end

@implementation MicAudioHelper

- (BOOL)startWithChunkCallback:(surround_encoder::AudioChunkCallback)callback
                     deviceUid:(const std::string&)device_uid
                         error:(std::string*)error {
  self.chunk = std::move(callback);
  self.engine = [AVAudioEngine new];

  AVAudioInputNode* input = self.engine.inputNode;
  AVAudioFormat* input_format = [input outputFormatForBus:0];

  __weak MicAudioHelper* weakSelf = self;
  [input installTapOnBus:0
              bufferSize:2048
                  format:input_format
                   block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
                     [weakSelf handleBuffer:buffer];
                   }];

  NSError* engine_error = nullptr;
  [self.engine prepare];
  if (![self.engine startAndReturnError:&engine_error]) {
    [input removeTapOnBus:0];
    self.engine = nullptr;
    *error = engine_error.localizedDescription != nullptr
                 ? std::string(engine_error.localizedDescription.UTF8String)
                 : std::string("failed to start audio engine");
    return NO;
  }
  return YES;
}

- (void)handleBuffer:(AVAudioPCMBuffer*)buffer {
  AVAudioFormat* format = buffer.format;
  const uint32_t ch = static_cast<uint32_t>(format.channelCount);
  const uint32_t frames = static_cast<uint32_t>(buffer.frameLength);
  if (frames == 0 || ch == 0) {
    return;
  }
  const float** planes =
      reinterpret_cast<const float**>(buffer.floatChannelData);
  std::vector<float> interleaved;
  interleaved.reserve(static_cast<size_t>(frames) * ch);
  for (uint32_t i = 0; i < frames; ++i) {
    for (uint32_t c = 0; c < ch; ++c) {
      interleaved.push_back(planes[c][i]);
    }
  }

  surround_encoder::CaptureFormat capture_format;
  capture_format.sample_rate = static_cast<uint32_t>(format.sampleRate);
  capture_format.channels = ch;
  self.chunk(interleaved.data(), frames, capture_format);
}

- (void)stop {
  if (self.engine != nullptr) {
    [self.engine.inputNode removeTapOnBus:0];
    [self.engine stop];
    self.engine = nullptr;
  }
}

@end

// ---------------------------------------------------------------------------
// Flutter plugin registration.

static std::mutex g_pipeline_mutex;
static surround_encoder::EncoderPipeline g_pipeline;

@interface SurroundEncoderPlugin : NSObject <SENSurroundEncoderPluginApi>
@property(nonatomic, strong) SENSurroundEncoderEventApi* event_api;
- (instancetype)initWithMessenger:(id<FlutterBinaryMessenger>)messenger;
+ (void)registerWithRegistrar:(NSObject<FlutterPluginRegistrar>*)registrar;
@end

@implementation SurroundEncoderPlugin

+ (void)registerWithRegistrar:(NSObject<FlutterPluginRegistrar>*)registrar {
  SurroundEncoderPlugin* plugin =
      [[SurroundEncoderPlugin alloc]
          initWithMessenger:registrar.messenger];
  [registrar publish:plugin];
}

- (instancetype)initWithMessenger:(id<FlutterBinaryMessenger>)messenger {
  if ((self = [super init])) {
    self.event_api =
        [[SENSurroundEncoderEventApi alloc] initWithBinaryMessenger:messenger];
  }
  return self;
}

- (void)listCaptureDevicesWithCompletion:
    (void (^)(NSArray<SENAudioDeviceInfo*>* _Nullable,
              FlutterError* _Nullable))completion {
  std::vector<surround_encoder::DeviceInfo> devices;
  @try {
    devices = surround_encoder::ListCaptureDevices();
  } @catch (NSException* exception) {
    completion(nullptr, [FlutterError errorWithCode:@"device_enumeration_failed"
                                            message:exception.reason
                                            details:nil]);
    return;
  }
  NSMutableArray<SENAudioDeviceInfo*>* result = [NSMutableArray new];
  for (const surround_encoder::DeviceInfo& info : devices) {
    SENCaptureSourceKindBox* kind = [[SENCaptureSourceKindBox alloc]
        initWithValue:info.kind == surround_encoder::SourceKind::kSystem
                          ? SENCaptureSourceKindSystem
                          : SENCaptureSourceKindMicrophone];
    [result addObject:[SENAudioDeviceInfo
                          makeWithKind:kind
                                    id:[NSString
                                           stringWithUTF8String:info.id
                                                                    .c_str()]
                                  name:[NSString
                                           stringWithUTF8String:info.name
                                                                    .c_str()]
                              isDefault:@(info.is_default)]];
  }
  completion(result, nullptr);
}

- (void)getCapabilitiesWithCompletion:
    (void (^)(SENPlatformCapabilities* _Nullable,
              FlutterError* _Nullable))completion {
  SENPlatformCapabilities* caps = [SENPlatformCapabilities new];
  caps.platformName = @"macOS";
  if (@available(macOS 13.0, *)) {
    caps.supportsSystemCapture = @YES;
    caps.notes =
        @"System capture requires screen-recording permission for the host "
        @"app.";
  } else {
    caps.supportsSystemCapture = @NO;
    caps.notes = @"System audio capture requires macOS 13.0+; microphone "
                 @"capture is available.";
  }
  caps.supportsMicrophone = @YES;
  completion(caps, nullptr);
}

- (void)startConfig:(SENEncoderConfig*)config
         completion:(void (^)(FlutterError* _Nullable))completion {
  std::lock_guard<std::mutex> lock(g_pipeline_mutex);
  if (g_pipeline.is_running()) {
    completion([FlutterError errorWithCode:@"start_failed"
                                   message:@"a session is already running"
                                   details:nil]);
    return;
  }

  surround_encoder::PipelineConfig pipeline;
  pipeline.capture_system = config.captureSystem.boolValue;
  pipeline.capture_mic = config.captureMicrophone.boolValue;
  if (config.systemDeviceId.length > 0) {
    pipeline.system_device_id = config.systemDeviceId.UTF8String;
  }
  if (config.microphoneDeviceId.length > 0) {
    pipeline.mic_device_id = config.microphoneDeviceId.UTF8String;
  }
  pipeline.out_channels = config.layout.value == SENChannelLayoutSurround51
                              ? 6
                              : 2;
  pipeline.sample_rate = 48000;
  pipeline.bitrate = config.bitrate.intValue;

  SENSinkKind sink_kind =
      config.sinkKind != nullptr ? config.sinkKind.value
                                 : SENSinkKindStream;
  if (sink_kind == SENSinkKindFile) {
    if (config.filePath.length == 0) {
      completion([FlutterError errorWithCode:@"start_failed"
                                     message:@"file sink requires a path"
                                     details:nil]);
      return;
    }
    pipeline.sink = surround_encoder::PipelineSink::kFile;
    pipeline.file_path = config.filePath.UTF8String;
  } else if (sink_kind == SENSinkKindUdp) {
    if (config.udpHost.length == 0 || config.udpPort == nullptr) {
      completion([FlutterError errorWithCode:@"start_failed"
                                     message:@"udp sink requires host and port"
                                     details:nil]);
      return;
    }
    pipeline.sink = surround_encoder::PipelineSink::kUdp;
    pipeline.udp_host = config.udpHost.UTF8String;
    pipeline.udp_port = config.udpPort.intValue;
  } else {
    pipeline.sink = surround_encoder::PipelineSink::kStream;
  }

  if (!pipeline.capture_system && !pipeline.capture_mic) {
    completion([FlutterError
        errorWithCode:@"start_failed"
              message:@"at least one capture source must be enabled"
              details:nil]);
    return;
  }

  g_pipeline.set_event_callback([self](const surround_encoder::PipelineEvent&
                                           event) {
    dispatch_async(dispatch_get_main_queue(), ^{
      SENEncoderEventData* data = [SENEncoderEventData new];
      switch (event.state) {
        case surround_encoder::PipelineState::kIdle:
          data.state = [[SENEncoderStateBox alloc]
              initWithValue:SENEncoderStateIdle];
          break;
        case surround_encoder::PipelineState::kStarting:
          data.state = [[SENEncoderStateBox alloc]
              initWithValue:SENEncoderStateStarting];
          break;
        case surround_encoder::PipelineState::kRunning:
          data.state = [[SENEncoderStateBox alloc]
              initWithValue:SENEncoderStateRunning];
          break;
        case surround_encoder::PipelineState::kStopped:
          data.state = [[SENEncoderStateBox alloc]
              initWithValue:SENEncoderStateStopped];
          break;
        case surround_encoder::PipelineState::kError:
          data.state = [[SENEncoderStateBox alloc]
              initWithValue:SENEncoderStateError];
          break;
      }
      if (!event.message.empty()) {
        data.message = [NSString
            stringWithUTF8String:event.message.c_str()];
      }
      data.encodedSeconds = @(event.encoded_seconds);
      data.bitrateKbps = @(event.bitrate_kbps);
      [self.event_api onEncoderEventEvent:data
                               completion:^(FlutterError* err){}];
    });
  });
  g_pipeline.set_packet_callback(
      [self](const uint8_t* data, size_t size) {
        NSData* payload = [NSData dataWithBytes:data length:size];
        FlutterStandardTypedData* typed =
            [FlutterStandardTypedData typedDataWithBytes:payload];
        dispatch_async(dispatch_get_main_queue(), ^{
          [self.event_api onEncodedPacketPacket:typed
                                     completion:^(FlutterError* err){}];
        });
      });

  std::string error;
  if (!g_pipeline.Start(pipeline, &error)) {
    completion([FlutterError
        errorWithCode:@"start_failed"
              message:[NSString stringWithUTF8String:error.c_str()]
              details:nil]);
    return;
  }
  completion(nullptr);
}

- (void)stopWithCompletion:
    (void (^)(FlutterError* _Nullable))completion {
  std::lock_guard<std::mutex> lock(g_pipeline_mutex);
  std::string error;
  if (!g_pipeline.Stop(&error) && !error.empty()) {
    completion([FlutterError
        errorWithCode:@"stop_failed"
              message:[NSString stringWithUTF8String:error.c_str()]
              details:nil]);
    return;
  }
  completion(nullptr);
}

@end
