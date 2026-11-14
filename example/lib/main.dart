import 'dart:async';

import 'package:material_ui/material_ui.dart';
import 'package:surround_encoder/surround_encoder.dart';

void main() {
  runApp(const ExampleApp());
}

class ExampleApp extends StatelessWidget {
  const ExampleApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'surround_encoder example',
      theme: ThemeData(colorSchemeSeed: Colors.indigo, useMaterial3: true),
      home: const EncoderPage(),
    );
  }
}

class EncoderPage extends StatefulWidget {
  const EncoderPage({super.key});

  @override
  State<EncoderPage> createState() => _EncoderPageState();
}

class _EncoderPageState extends State<EncoderPage> {
  EncoderCapabilities? _capabilities;
  List<CaptureDevice> _devices = const <CaptureDevice>[];
  bool _running = false;
  double _encodedSeconds = 0;
  double _bitrateKbps = 0;
  SinkKind _sinkKind = SinkKind.file;
  ChannelLayout _layout = ChannelLayout.surround51;
  bool _captureMic = false;
  final List<String> _log = <String>[];
  StreamSubscription<EncoderEvent>? _eventSub;

  // Compile-time switch for automated end-to-end runs (CI smoke tests):
  // flutter run --dart-define=SURROUND_AUTO_START=true starts a short
  // file-sink session immediately and stops it after ten seconds.
  static const bool _autoStart =
      bool.fromEnvironment('SURROUND_AUTO_START');

  @override
  void initState() {
    super.initState();
    _refresh();
    if (_autoStart) {
      _runAutoStartSession();
    }
    _eventSub = SurroundEncoder.events.listen(
      (EncoderEvent event) {
        setState(() {
          if (event.state == EncoderState.running ||
              event.state == EncoderState.stopped ||
              event.state == EncoderState.error) {
            _running = event.state == EncoderState.running;
          }
          if (event.encodedSeconds != null) {
            _encodedSeconds = event.encodedSeconds!;
          }
          if (event.bitrateKbps != null) {
            _bitrateKbps = event.bitrateKbps!;
          }
          if (event.message != null) {
            _log.insert(0, '[${event.state.name}] ${event.message}');
            if (_log.length > 20) {
              _log.removeLast();
            }
          }
        });
      },
      onError: (Object error) {
        setState(() {
          _log.insert(0, '[error] $error');
          _running = false;
        });
      },
    );
  }

  @override
  void dispose() {
    _eventSub?.cancel();
    super.dispose();
  }

  Future<void> _refresh() async {
    try {
      final EncoderCapabilities caps = await SurroundEncoder.capabilities();
      final List<CaptureDevice> devices = await SurroundEncoder.listDevices();
      if (!mounted) {
        return;
      }
      setState(() {
        _capabilities = caps;
        _devices = devices;
      });
    } catch (e) {
      if (!mounted) {
        return;
      }
      setState(() => _log.insert(0, '[error] $e'));
    }
  }

  Future<void> _runAutoStartSession() async {
    const String path = 'surround_auto_start_out.ts';
    try {
      await SurroundEncoder.start(const SurroundEncoderConfig(
        captureSystem: true,
        sink: FileSink(path),
      ));
      await Future<void>.delayed(const Duration(seconds: 10));
      await SurroundEncoder.stop();
      debugPrint('SURROUND_AUTO_START session finished, output: $path');
    } catch (e) {
      debugPrint('SURROUND_AUTO_START session failed: $e');
    }
  }

  Future<void> _start() async {
    try {
      final String path =
          'surround_out_${DateTime.now().millisecondsSinceEpoch}.ts';
      final SinkConfig sink = switch (_sinkKind) {
        SinkKind.file => FileSink(path),
        SinkKind.udp => const UdpSink('239.255.0.1', 1234),
        SinkKind.stream => const StreamSink(),
      };
      await SurroundEncoder.start(SurroundEncoderConfig(
        captureSystem: true,
        captureMicrophone: _captureMic,
        layout: _layout,
        sink: sink,
      ));
      setState(() {
        _encodedSeconds = 0;
        _bitrateKbps = 0;
        _log.insert(0, '[start] sink=${_sinkKind.name}, layout=${_layout.name}');
      });
    } catch (e) {
      setState(() => _log.insert(0, '[error] $e'));
    }
  }

  Future<void> _stop() async {
    try {
      await SurroundEncoder.stop();
    } catch (e) {
      setState(() => _log.insert(0, '[error] $e'));
    }
  }

  @override
  Widget build(BuildContext context) {
    final EncoderCapabilities? caps = _capabilities;
    return Scaffold(
      appBar: AppBar(title: const Text('surround_encoder example')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: <Widget>[
          Text(caps?.toString() ?? 'Loading capabilities...'),
          const SizedBox(height: 8),
          ..._devices.map((CaptureDevice d) => Text(d.toString())),
          const Divider(height: 24),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: <Widget>[
              SegmentedButton<SinkKind>(
                segments: const <ButtonSegment<SinkKind>>[
                  ButtonSegment<SinkKind>(
                      value: SinkKind.file, label: Text('File')),
                  ButtonSegment<SinkKind>(
                      value: SinkKind.udp, label: Text('UDP')),
                  ButtonSegment<SinkKind>(
                      value: SinkKind.stream, label: Text('Stream')),
                ],
                selected: <SinkKind>{_sinkKind},
                onSelectionChanged: _running
                    ? null
                    : (Set<SinkKind> s) =>
                        setState(() => _sinkKind = s.first),
              ),
              SegmentedButton<ChannelLayout>(
                segments: const <ButtonSegment<ChannelLayout>>[
                  ButtonSegment<ChannelLayout>(
                      value: ChannelLayout.stereo, label: Text('2.0')),
                  ButtonSegment<ChannelLayout>(
                      value: ChannelLayout.surround51, label: Text('5.1')),
                ],
                selected: <ChannelLayout>{_layout},
                onSelectionChanged: _running
                    ? null
                    : (Set<ChannelLayout> s) =>
                        setState(() => _layout = s.first),
              ),
            ],
          ),
          SwitchListTile(
            title: const Text('Capture microphone'),
            subtitle: Text(
                'System audio is always captured${caps?.supportsSystemCapture == false ? ' (unavailable on this platform!)' : ''}'),
            value: _captureMic,
            onChanged: _running
                ? null
                : (bool v) => setState(() => _captureMic = v),
          ),
          const SizedBox(height: 8),
          Row(
            children: <Widget>[
              FilledButton(
                onPressed: _running ? null : _start,
                child: const Text('Start'),
              ),
              const SizedBox(width: 12),
              OutlinedButton(
                onPressed: _running ? _stop : null,
                child: const Text('Stop'),
              ),
            ],
          ),
          const Divider(height: 24),
          Text(
            'Encoded: ${_encodedSeconds.toStringAsFixed(1)} s @ '
            '${_bitrateKbps.toStringAsFixed(0)} kbps',
            style: Theme.of(context).textTheme.titleMedium,
          ),
          const SizedBox(height: 8),
          for (final String line in _log) Text(line, style: const TextStyle(fontSize: 12)),
        ],
      ),
    );
  }
}
