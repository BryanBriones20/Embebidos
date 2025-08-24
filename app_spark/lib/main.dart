import 'dart:async';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

void main() => runApp(const MyApp());

class MyApp extends StatelessWidget {
  const MyApp({super.key});
  @override
  Widget build(BuildContext context) {
    return MaterialApp(title: 'Robot Control', debugShowCheckedModeBanner: false, home: const HomePage());
  }
}

enum Mode { manual, automatic }
enum AutoPhase { transition, idle, detecting, classifying, pregrip, pickplace, homing }

class HomePage extends StatefulWidget {
  const HomePage({super.key});
  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  // --- Red / Endpoints ---
  String _baseUrl = 'http://192.168.0.100';
  Future<void> _sendGet(String path) async {
    try { await http.get(Uri.parse('$_baseUrl$path')).timeout(const Duration(seconds: 2)); } catch (_) {}
  }
  Future<Map<String, dynamic>?> _fetchState() async {
    try {
      final res = await http.get(Uri.parse('$_baseUrl/state')).timeout(const Duration(seconds: 2));
      if (res.statusCode == 200) return jsonDecode(res.body) as Map<String, dynamic>;
    } catch (_) {}
    return null;
  }

  // --- Estado UI / backend ---
  bool running = false;
  Mode mode = Mode.manual;
  AutoPhase autoPhase = AutoPhase.idle;
  String clasif = '';
  bool obj = false;
  int beltSpeed = 150;
  double speedDps = 180;

  List<int> angles = List.filled(5, 0); // sliders
  Timer? _debounce;
  Timer? _pollTimer;

  // --- Utilidades ---
  String _normalizeBaseUrl(String input) {
    var s = input.trim();
    if (!s.startsWith('http://') && !s.startsWith('https://')) s = 'http://$s';
    if (s.endsWith('/')) s = s.substring(0, s.length - 1);
    return s;
  }

  AutoPhase _phaseFromStr(String p) {
    switch (p) {
      case 'transition': return AutoPhase.transition;
      case 'idle':       return AutoPhase.idle;
      case 'detecting':  return AutoPhase.detecting;
      case 'classifying':return AutoPhase.classifying;
      case 'pregrip':    return AutoPhase.pregrip;
      case 'pickplace':  return AutoPhase.pickplace;
      case 'homing':     return AutoPhase.homing;
      default:           return AutoPhase.idle;
    }
  }

  // --- Enlaces con firmware ---
  Future<void> _applyRun() async {
    await _sendGet('/run?on=${running ? 1 : 0}');
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(running ? 'Sistema en MARCHA' : 'Sistema en PARO')));
  }

  Future<void> _applyMode() async {
    final isAuto = mode == Mode.automatic ? 1 : 0;
    await _sendGet('/mode?auto=$isAuto');
    if (mode == Mode.automatic) {
      // Mostrar transición mientras el firmware va a HOME
      setState(() => autoPhase = AutoPhase.transition);
      _startPolling();
    } else {
      _stopPolling();
      // Al volver a manual: sincronizar sliders con la pose actual
      final st = await _fetchState();
      if (st != null && st['current'] is List) {
        final curr = (st['current'] as List).cast<num>().map((e)=>e.toInt()).toList();
        setState(() {
          angles = List.generate(5, (i) => i < curr.length ? curr[i] : 0);
          running = (st['running'] == true);
          speedDps = (st['speed_dps'] as num?)?.toDouble() ?? speedDps;
        });
      }
    }
  }

  Future<void> _applySpeed(double dps) async {
    final v = dps.clamp(1, 720).round();
    await _sendGet('/speed?dps=$v');
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Velocidad: $v °/s')));
  }

  // --- Sliders manuales ---
  void _onSliderChange(int index, double value) {
    setState(() => angles[index] = value.round());
    _debounce?.cancel();
    _debounce = Timer(const Duration(milliseconds: 150), _sendAngles);
  }
  Future<void> _sendAngles() async {
    final q = List.generate(5, (i) => 'a${i + 1}=${angles[i]}').join('&');
    await _sendGet('/sliders?$q');
  }

  // --- Polling de /state en Automático ---
  void _startPolling() {
    _pollTimer?.cancel();
    _pollTimer = Timer.periodic(const Duration(milliseconds: 700), (_) async {
      final st = await _fetchState();
      if (!mounted || st == null) return;
      setState(() {
        speedDps = (st['speed_dps'] as num?)?.toDouble() ?? speedDps;
        running  = (st['running'] == true);
        clasif   = (st['clasif'] as String?) ?? '';
        obj      = (st['obj'] == true);
        beltSpeed= (st['belt_speed'] as num?)?.toInt() ?? beltSpeed;
        autoPhase= _phaseFromStr((st['phase'] as String?) ?? 'idle');
      });
    });
  }
  void _stopPolling() { _pollTimer?.cancel(); _pollTimer = null; }

  @override
  void dispose() { _debounce?.cancel(); _stopPolling(); super.dispose(); }

  // --- UI ----
  void _openIpDialog() {
    final c = TextEditingController(text: _baseUrl);
    showDialog(
      context: context,
      builder: (_) => AlertDialog(
        title: const Text('Ingresar IP/URL del robot'),
        content: TextField(
          controller: c,
          decoration: const InputDecoration(labelText: 'Ej: 192.168.1.50 o http://robot.local'),
          keyboardType: TextInputType.url,
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(context), child: const Text('Cancelar')),
          ElevatedButton(
            onPressed: () {
              final normalized = _normalizeBaseUrl(c.text);
              setState(() => _baseUrl = normalized);
              Navigator.pop(context);
              ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Usando: $normalized')));
            },
            child: const Text('Guardar'),
          ),
        ],
      ),
    );
  }

  void _openSpeedSheet() {
    double temp = speedDps;
    showModalBottomSheet(
      context: context,
      showDragHandle: true,
      builder: (_) => StatefulBuilder(builder: (context, setLocal) {
        return Padding(
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 24),
          child: Column(mainAxisSize: MainAxisSize.min, children: [
            const Text('Velocidad (grados/seg)', style: TextStyle(fontSize: 18, fontWeight: FontWeight.w600)),
            const SizedBox(height: 8),
            Text('${temp.round()} °/s'),
            Slider(
              min: 30, max: 360, divisions: 330,
              value: temp,
              label: '${temp.round()}',
              onChanged: (v) => setLocal(() => temp = v),
              onChangeEnd: (v) async { setState(() => speedDps = v); await _applySpeed(v); },
            ),
            const SizedBox(height: 8),
            const Text('Consejo: 120–220 °/s suele sentirse muy fluido.'),
          ]),
        );
      }),
    );
  }

  String _phaseLabel(AutoPhase p) {
    switch (p) {
      case AutoPhase.transition: return 'Transición a home';
      case AutoPhase.idle:       return 'En espera';
      case AutoPhase.detecting:  return 'Detectando objeto';
      case AutoPhase.classifying:return 'Clasificando';
      case AutoPhase.pregrip:    return 'Posición de agarre';
      case AutoPhase.pickplace:  return 'Pick & Place';
      case AutoPhase.homing:     return 'Regresando a home';
    }
  }

  List<AutoPhase> get _orderedPhases => const [
    AutoPhase.transition, AutoPhase.idle, AutoPhase.detecting,
    AutoPhase.classifying, AutoPhase.pregrip, AutoPhase.pickplace, AutoPhase.homing,
  ];
  int _phaseIndex(AutoPhase p) => _orderedPhases.indexOf(p);

  Widget _buildModeSelector() {
    return SegmentedButton<Mode>(
      segments: const [
        ButtonSegment(value: Mode.manual, icon: Icon(Icons.tune), label: Text('Manual')),
        ButtonSegment(value: Mode.automatic, icon: Icon(Icons.memory), label: Text('Automático')),
      ],
      selected: {mode},
      onSelectionChanged: (s) async {
        setState(() => mode = s.first);
        await _applyMode();
      },
    );
  }

  Widget _buildRunBar() {
    return Row(children: [
      Expanded(
        child: ElevatedButton.icon(
          onPressed: () async { setState(() => running = !running); await _applyRun(); },
          icon: Icon(running ? Icons.pause_circle_filled : Icons.play_circle_fill),
          label: Text(running ? 'PARO' : 'MARCHA'),
          style: ElevatedButton.styleFrom(padding: const EdgeInsets.symmetric(vertical: 14)),
        ),
      ),
      const SizedBox(width: 12),
      IconButton(tooltip: 'Cambiar IP/URL', onPressed: _openIpDialog, icon: const Icon(Icons.settings_ethernet)),
      IconButton(tooltip: 'Velocidad', onPressed: _openSpeedSheet, icon: const Icon(Icons.speed)),
    ]);
  }

  Widget _buildManual() {
    return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      const Text('Control manual', style: TextStyle(fontSize: 18, fontWeight: FontWeight.w600)),
      const SizedBox(height: 8),
      Text('Velocidad actual: ${speedDps.round()} °/s'),
      const SizedBox(height: 8),
      ListView.builder(
        shrinkWrap: true, physics: const NeverScrollableScrollPhysics(), itemCount: 5,
        itemBuilder: (_, i) => Card(
          margin: const EdgeInsets.symmetric(vertical: 6),
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text('Ángulo ${i + 1}: ${angles[i]}°'),
              Slider(
                value: angles[i].toDouble(), min: 0, max: 180, divisions: 180, label: angles[i].toString(),
                onChanged: running && mode == Mode.manual ? (v) => _onSliderChange(i, v) : null,
              ),
            ]),
          ),
        ),
      ),
      if (!(running && mode == Mode.manual))
        const Padding(padding: EdgeInsets.only(top: 8), child: Text('Activa MARCHA y selecciona MANUAL para mover.', style: TextStyle(fontStyle: FontStyle.italic))),
    ]);
  }

  Widget _buildAutoTimeline() {
    final idx = _phaseIndex(autoPhase);
    final total = _orderedPhases.length - 1;
    final progress = (idx / (total == 0 ? 1 : total)).clamp(0.0, 1.0);

    return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      const Text('Modo automático', style: TextStyle(fontSize: 18, fontWeight: FontWeight.w600)),
      const SizedBox(height: 8),
      Row(children: [
        const Icon(Icons.info_outline), const SizedBox(width: 8),
        Expanded(child: Text('Estado: ${_phaseLabel(autoPhase)}')),
      ]),
      const SizedBox(height: 8),
      if (clasif.isNotEmpty) Text('Clasificación: $clasif'),
      Row(children: [
        const Icon(Icons.inventory_2_outlined), const SizedBox(width: 6),
        Text(obj ? 'Objeto detectado' : 'Sin objeto'),
      ]),
      const SizedBox(height: 12),
      LinearProgressIndicator(value: progress),
      const SizedBox(height: 12),
      Column(children: _orderedPhases.map((p) {
        final i = _phaseIndex(p);
        final isPast = i < idx, isCurrent = i == idx, last = i == total;
        return _TimelineTile(title: _phaseLabel(p), past: isPast, current: isCurrent, last: last);
      }).toList()),
    ]);
  }

  @override
  Widget build(BuildContext context) {
    final shown = _baseUrl.replaceFirst(RegExp(r'^https?://'), '');
    return Scaffold(
      appBar: AppBar(title: Text('Robot ($shown)'), actions: [Padding(padding: const EdgeInsets.symmetric(horizontal: 8), child: Center(child: _buildModeSelector()))]),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(16),
        child: Column(children: [
          _buildRunBar(),
          const SizedBox(height: 16),
          if (mode == Mode.manual) _buildManual() else _buildAutoTimeline(),
        ]),
      ),
    );
  }
}

// ===== Timeline widget =====
class _TimelineTile extends StatelessWidget {
  final String title; final bool past; final bool current; final bool last;
  const _TimelineTile({required this.title, required this.past, required this.current, required this.last});
  @override
  Widget build(BuildContext context) {
    const dotSize = 14.0, lineWidth = 3.0;
    return Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Column(children: [
        Container(width: dotSize, height: dotSize, decoration: BoxDecoration(shape: BoxShape.circle,
          color: current ? Colors.blueAccent : (past ? Colors.green : Colors.grey.shade400),
        )),
        if (!last) Container(width: lineWidth, height: 28, margin: const EdgeInsets.only(top: 2),
          decoration: BoxDecoration(color: past ? Colors.green : Colors.grey.shade400, borderRadius: BorderRadius.circular(2))),
      ]),
      const SizedBox(width: 10),
      Expanded(child: Padding(padding: const EdgeInsets.only(top: 0), child: Text(title, style: TextStyle(fontWeight: current ? FontWeight.w700 : FontWeight.w400)))),
    ]);
  }
}
