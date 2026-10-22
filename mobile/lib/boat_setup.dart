import 'boat_wifi.dart';

/// The one button a setup step offers.
enum SetupAction { retry }

/// The words and button for one [WifiSetup] step.
class SetupCopy {
  const SetupCopy({
    required this.title,
    required this.body,
    this.hint,
    this.action,
    this.actionLabel,
    this.working = false,
  });

  final String title;
  final String body;
  final String? hint;
  final SetupAction? action;
  final String? actionLabel;

  /// The app is waiting on Android or the terminal, not on the user.
  final bool working;
}

SetupCopy setupCopy(WifiSetup setup, String ssid, {String? problem}) {
  switch (setup) {
    case WifiSetup.reading:
      return const SetupCopy(
        title: 'Getting the boat Wi-Fi',
        body: 'Reading the Wi-Fi name from the terminal.',
        working: true,
      );
    case WifiSetup.joining:
      return SetupCopy(
        title: 'Connecting to the boat Wi-Fi',
        body:
            'Joining $ssid for the LMYC app. If Android asks, tap Connect. '
            'Your phone\'s internet carries on over mobile data.',
        working: true,
      );
    case WifiSetup.noAnswer:
      return SetupCopy(
        title: 'Waiting for the terminal',
        body:
            'The phone is on $ssid. Waiting for the boat data. '
            'If this lasts more than a minute, restart the terminal.',
        working: true,
      );
    case WifiSetup.problem:
      return SetupCopy(
        title: 'Could not get the boat Wi-Fi',
        body: problem ?? 'The terminal did not send its Wi-Fi details.',
        action: SetupAction.retry,
        actionLabel: 'Try again',
      );
    case WifiSetup.ready:
      return const SetupCopy(
        title: 'Ready for chart apps',
        body:
            'In Navionics or i-Boating, add a UDP device on port 10110. '
            'If it asks for an address, use $kRelayHost.',
      );
  }
}
