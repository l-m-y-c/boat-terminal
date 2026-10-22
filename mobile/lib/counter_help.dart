/// What each counter on the NMEA data screen means, worked out from the
/// firmware that produces it: firmware/src/n2k.cpp (receive side),
/// firmware/src/gateway.cpp (chart-app side) and terminal_stats.cpp (the
/// JSON on BLE …000050). Keep these in step with that code.
library;

class CounterHelp {
  const CounterHelp(this.title, this.paragraphs);

  final String title;
  final List<String> paragraphs;
}

const _sinceStart =
    'Counted since the terminal last started. Switching the data source '
    'restarts the terminal, so that resets it too.';

const counterHelp = <String, CounterHelp>{
  'mps': CounterHelp('Messages per second', [
    'Complete NMEA 2000 messages (PGNs) the terminal decoded in the last '
        'second.',
    'Big messages, such as a GNSS position, travel as several CAN frames '
        '("fast packet") and count once here, after they are put back '
        'together. So this is always at or below Frames per second.',
    'In simulation it counts the simulator\'s messages, not the boat\'s.',
  ]),
  'fps': CounterHelp('Frames per second', [
    'Raw CAN frames the terminal received from the NMEA 2000 backbone in '
        'the last second.',
    'A short message is one frame; a long one is several. The backbone '
        'runs at 250 kbit/s, which is roughly 1,800 to 2,000 frames per '
        'second at 100% load, so a few hundred is a healthy, busy boat.',
    'Frames that are not NMEA 2000 (11-bit or remote frames) are not '
        'counted here.',
  ]),
  'fps_peak': CounterHelp('Peak frames per second', [
    'The highest Frames per second seen in any one second.',
    _sinceStart,
    'Useful for sizing: compare it with roughly 1,800 to 2,000 frames per '
        'second, which is a full 250 kbit/s backbone.',
  ]),
  'frames': CounterHelp('Frames received', [
    'Every NMEA 2000 CAN frame that reached the terminal\'s receive task.',
    'It includes frames that were then dropped because the receive buffer '
        'was full (those also show in Lost frames). Frames the CAN driver or '
        'controller dropped before that point are only in Lost frames.',
    _sinceStart,
  ]),
  'pgns': CounterHelp('PGN types', [
    'How many different kinds of NMEA 2000 message (PGN numbers) the '
        'terminal has decoded, for example 128267 water depth or 130306 '
        'wind.',
    'It only grows: a PGN heard once stays counted. The terminal remembers '
        'up to 96 kinds.',
    _sinceStart,
  ]),
  'devices': CounterHelp('Devices', [
    'How many different NMEA 2000 source addresses have sent a message, '
        'which is roughly the number of devices talking on the backbone '
        '(chartplotter, depth sounder, VHF, …).',
    'It only grows. A device that is switched off stays counted, and a '
        'device that changes its address counts again. The terminal only '
        'listens, so it never counts itself.',
    _sinceStart,
  ]),
  'lost': CounterHelp('Lost frames', [
    'CAN frames that arrived but were never decoded. It is the sum of '
        'three places a frame can be dropped:',
    '• Receive buffer full: the decoder fell behind and the 2,048-frame '
        'buffer had no room.\n'
        '• Driver queue full: the CAN driver\'s 256-frame queue overflowed, '
        'including frames still in it when the interface was stopped after '
        'a bus fault.\n'
        '• Controller overrun: the CAN chip\'s own small buffer overflowed. '
        'The chip reports the frames it clears, so this part is a lower '
        'bound.',
    'It should stay at 0. A lost frame is a missed instrument update, and '
        'can spoil a multi-frame message such as a position.',
    _sinceStart,
  ]),
  'bus_err': CounterHelp('Bus errors', [
    'CAN error frames the terminal\'s controller has seen on the wire: '
        'signals that some device could not read or send a frame cleanly.',
    'A few when a cable is plugged in are normal. A number that keeps '
        'rising points at the wiring: a missing or extra terminator, a '
        'loose drop cable, a backbone that is too long, or electrical '
        'noise.',
    'It adds up across restarts of the CAN interface. $_sinceStart',
  ]),
  'rec': CounterHelp('Error counter', [
    'The CAN controller\'s receive error counter (REC), as it is right '
        'now. It is not a total.',
    'The controller adds to it for every frame it receives badly and '
        'takes one off for every good frame, so on a healthy bus it sits at '
        '0. A value that stays up means receive errors are happening now; '
        'above 127 the controller is "error passive".',
  ]),
  'rx_buffer': CounterHelp('Receive buffer', [
    'The buffer between the CAN receiver and the decoder. It holds 2,048 '
        'frames.',
    'Peak is the most frames that were ever waiting at once; now is how '
        'many are waiting at this moment. The bar shows the peak as a share '
        'of the buffer.',
    'If it ever fills, new frames are dropped and counted in Lost frames. '
        'A low peak means the terminal keeps up with the bus easily.',
  ]),
  'apps': CounterHelp('Connected', [
    'TCP connections to the terminal\'s NMEA port (10110) right now. The '
        'terminal takes up to 4.',
    'This phone\'s link counts as one, however many chart apps on the '
        'phone use it. Other phones or laptops connected directly count too.',
    'Apps listening for UDP broadcasts are not counted: the terminal '
        'cannot see them.',
  ]),
  'out': CounterHelp('Sentences sent', [
    'NMEA 0183 sentences the terminal has sent out to chart apps, counted '
        'once per sentence however many apps get it.',
    'It includes the heartbeat sentence (\$IIHBT) the terminal sends every '
        '15 seconds so phones can tell a quiet bus from a lost link.',
    _sinceStart,
  ]),
  'not_delivered': CounterHelp('Not delivered', [
    'Sentences the terminal could not hand to a chart app. Two things add '
        'up here:',
    '• A TCP app fell behind: its 8 KB queue on the terminal was full when '
        'a sentence was due, so that app skipped the sentence. It counts '
        'once per app that missed it. An app that takes nothing for 30 '
        'seconds is disconnected.\n'
        '• A UDP broadcast failed: the terminal\'s Wi-Fi had no buffer free. '
        'Every sentence is broadcast on two ports (10110 and 2000), so one '
        'sentence can count twice.',
    'It does not count UDP packets lost over the air (nothing reports '
        'those), or anything between this phone and the chart apps on it.',
    'A few during a Wi-Fi hiccup are harmless. A number that keeps rising '
        'means a phone or app has stopped reading.',
    _sinceStart,
  ]),
  'app_buffer': CounterHelp('App buffer', [
    'Each TCP chart app has an 8 KB queue on the terminal, which is several '
        'seconds of sentences. It lets a phone that pauses its Wi-Fi for a '
        'moment catch up without losing anything.',
    'Peak is the most that was ever waiting for any one app. Near 100% '
        'means an app or phone stopped reading for a while; when the queue '
        'is full, sentences for that app go to Not delivered.',
    _sinceStart,
  ]),
};
