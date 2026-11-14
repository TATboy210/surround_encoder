// This is a basic Flutter integration test.
//
// Since integration tests run in a full Flutter application, they can interact
// with the host side of a plugin implementation, unlike Dart unit tests.
//
// For more information about Flutter integration tests, please see
// https://flutter.dev/to/integration-testing

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

import 'package:surround_encoder/surround_encoder.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('capabilities and device enumeration round-trip', (
    WidgetTester tester,
  ) async {
    final EncoderCapabilities caps = await SurroundEncoder.capabilities();
    // The full matrix varies by platform, but every backend reports its name.
    expect(caps.platformName.isNotEmpty, true);
    expect(caps.supportsMicrophone, true);

    final List<CaptureDevice> devices = await SurroundEncoder.listDevices();
    // At minimum a microphone path exists on every supported platform.
    expect(devices, isNotEmpty);
  });
}
