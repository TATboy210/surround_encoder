#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint surround_encoder.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'surround_encoder'
  s.version          = '0.0.1'
  s.summary          = 'Real-time surround-sound capture and encoding plugin.'
  s.description      = <<-DESC
Captures system playback audio and microphone input and encodes it to a
surround stream using FFmpeg's open AC-3 implementation.
                       DESC
  s.homepage         = 'http://example.com'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'surround_encoder authors' => 'surround_encoder@example.com' }

  s.source           = { :path => '.' }
  s.source_files = 'surround_encoder/Sources/surround_encoder/**/*', '../src/common/**/*.{h,cpp}'

  # If your plugin requires a privacy manifest, for example if it collects user
  # data, update the PrivacyInfo.xcprivacy file to describe your plugin's
  # privacy impact, and then uncomment this line. For more information,
  # see https://developer.apple.com/documentation/bundleresources/privacy_manifest_files
  # s.resource_bundles = {'surround_encoder_privacy' => ['surround_encoder/Sources/surround_encoder/PrivacyInfo.xcprivacy']}

  s.dependency 'FlutterMacOS'

  s.platform = :osx, '10.15'

  # FFmpeg is not vendored in this repository (see NOTICE.md). Point
  # SURROUND_ENCODER_FFMPEG_ROOT at a dev layout (include/ + lib/) built by
  # tool/setup_ffmpeg.sh.
  ffmpeg_root = ENV['SURROUND_ENCODER_FFMPEG_ROOT']
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'HEADER_SEARCH_PATHS' => '"$(inherited)" "$(PODS_ROOT)/../../src/common"',
  }
  if ffmpeg_root
    s.pod_target_xcconfig['HEADER_SEARCH_PATHS'] = %Q{"$(inherited)" "$(PODS_ROOT)/../../src/common" "#{ffmpeg_root}/include"}
    s.pod_target_xcconfig['OTHER_LDFLAGS'] = %Q{"$(inherited)" -L"#{ffmpeg_root}/lib" -lavcodec -lavformat -lswresample -lavutil}
  end
end
