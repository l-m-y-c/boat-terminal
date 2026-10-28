import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lmyc_app/app_settings.dart';
import 'package:lmyc_app/main.dart';
import 'package:lmyc_app/tab_pager.dart';
import 'package:lmyc_app/nmea_data_page.dart';
import 'package:lmyc_app/terminal_stats.dart';

const _names = ['Boat', 'Instruments', 'NMEA data', 'Network', 'Diagnostics'];

class _Harness extends StatefulWidget {
  const _Harness();
  @override
  State<_Harness> createState() => _HarnessState();
}

class _HarnessState extends State<_Harness> {
  int tab = 0;
  @override
  Widget build(BuildContext context) => MaterialApp(
    home: Scaffold(
      body: WrapTabPager(
        count: _names.length,
        index: tab,
        onChanged: (i) => setState(() => tab = i),
        builder: (context, i) =>
            ListView(children: [Center(child: Text('page ${_names[i]}'))]),
      ),
      bottomNavigationBar: NavigationBar(
        selectedIndex: tab,
        onDestinationSelected: (i) => setState(() => tab = i),
        destinations: [
          for (final n in _names)
            NavigationDestination(icon: const Icon(Icons.circle), label: n),
        ],
      ),
    ),
  );
}

Future<void> _swipe(WidgetTester tester, double dx) async {
  await tester.fling(
    find.byKey(const ValueKey('tab-pager')),
    Offset(dx, 0),
    1200,
  );
  await tester.pumpAndSettle();
}

int _selected(WidgetTester tester) =>
    tester.widget<NavigationBar>(find.byType(NavigationBar)).selectedIndex;

void main() {
  testWidgets('swipe left goes forward and wraps Diagnostics -> Boat', (
    tester,
  ) async {
    await tester.pumpWidget(const _Harness());
    expect(find.text('page Boat'), findsOneWidget);
    for (final expected in [1, 2, 3, 4, 0, 1]) {
      await _swipe(tester, -300);
      expect(_selected(tester), expected);
      expect(find.text('page ${_names[expected]}'), findsOneWidget);
    }
  });

  testWidgets('swipe right goes back and wraps Boat -> Diagnostics', (
    tester,
  ) async {
    await tester.pumpWidget(const _Harness());
    for (final expected in [4, 3, 2, 1, 0, 4]) {
      await _swipe(tester, 300);
      expect(_selected(tester), expected);
      expect(find.text('page ${_names[expected]}'), findsOneWidget);
    }
  });

  testWidgets(
    'bottom bar tap moves the pager and swiping continues from there',
    (tester) async {
      await tester.pumpWidget(const _Harness());
      await tester.tap(find.text('Diagnostics'));
      await tester.pumpAndSettle();
      expect(find.text('page Diagnostics'), findsOneWidget);
      await _swipe(tester, -300);
      expect(_selected(tester), 0);
      expect(find.text('page Boat'), findsOneWidget);
    },
  );

  test('the bar has the five tabs in order', () {
    expect(OnBoardTab.values.map((t) => t.name), [
      'boat',
      'instruments',
      'nmeaData',
      'network',
      'diagnostics',
    ]);
    expect(kOnBoardDestinations.map((d) => d.label), _names);
    expect(
      StartTab.values.map((t) => t.name),
      OnBoardTab.values.map((t) => t.name),
    );
  });

  for (final scale in [1.0, 1.3, 2.0]) {
    testWidgets('five labels fit a 320 dp phone at ${scale}x text', (
      tester,
    ) async {
      tester.view.physicalSize = const Size(320, 640);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      await tester.pumpWidget(
        MaterialApp(
          builder: (context, child) => MediaQuery(
            data: MediaQuery.of(context)
                .copyWith(textScaler: TextScaler.linear(scale)),
            child: child!,
          ),
          home: Builder(
            builder: (context) => Scaffold(
              bottomNavigationBar: tabBarTextScale(
                context,
                NavigationBar(
                  selectedIndex: 2,
                  destinations: kOnBoardDestinations,
                ),
              ),
            ),
          ),
        ),
      );
      expect(tester.takeException(), isNull);
      for (final n in _names) {
        expect(find.text(n), findsOneWidget);
        expect(
          tester.getSize(find.text(n)).width,
          lessThanOrEqualTo(320 / 5),
          reason: n,
        );
      }
    });
  }

  testWidgets('vertical scrolling inside a tab does not change tabs', (
    tester,
  ) async {
    await tester.pumpWidget(const _Harness());
    await tester.fling(find.text('page Boat'), const Offset(0, -300), 1200);
    await tester.pumpAndSettle();
    expect(_selected(tester), 0);
  });

  group('Boat tab', () {
    Widget boat({BusState? state, bool linked = true}) => MaterialApp(
      home: Scaffold(
        body: BoatTabView(
          state: state,
          linked: linked,
          onReconnect: () {},
          onChartApp: () {},
          onLeave: () {},
        ),
      ),
    );

    testWidgets('has no status panel, keeps the actions', (tester) async {
      await tester.pumpWidget(boat(state: BusState.live));
      expect(
        find.textContaining('Chart apps on this phone can get'),
        findsNothing,
      );
      expect(find.byType(DataStatusPill), findsNothing);
      expect(find.text('Connect a chart app'), findsOneWidget);
      expect(find.text('Leave the boat'), findsOneWidget);
    });

    testWidgets('still warns about a bus fault and a Bluetooth drop', (
      tester,
    ) async {
      await tester.pumpWidget(boat(state: BusState.busOff, linked: false));
      expect(find.textContaining('bus has a fault'), findsOneWidget);
      expect(find.text('Reconnect'), findsOneWidget);
    });
  });
}
