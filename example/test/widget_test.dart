import 'package:flutter_test/flutter_test.dart';
import 'package:surround_encoder_example/main.dart';

void main() {
  testWidgets('example app builds and shows the encoder page', (
    WidgetTester tester,
  ) async {
    await tester.pumpWidget(const ExampleApp());
    expect(find.text('surround_encoder example'), findsOneWidget);
  });
}
