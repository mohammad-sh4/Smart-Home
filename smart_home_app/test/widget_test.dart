// This is a basic Flutter widget test for Smart Home App.

import 'package:flutter_test/flutter_test.dart';
import 'package:provider/provider.dart';

import 'package:smart_home_app/main.dart';
import 'package:smart_home_app/providers/home_provider.dart';

void main() {
  testWidgets('Smart Home App loads correctly', (WidgetTester tester) async {
    // Build our app and trigger a frame.
    await tester.pumpWidget(
      ChangeNotifierProvider(
        create: (context) => HomeProvider(),
        child: const SmartHomeApp(),
      ),
    );

    // Verify that the home screen loads
    expect(find.text('Smart Home Control'), findsOneWidget);
  });
}
