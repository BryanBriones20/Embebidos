import 'dart:async';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

void main() => runApp(const MyApp());

class MyApp extends StatelessWidget {
  const MyApp({super.key});
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Sliders Control',
      home: const SliderPage(),
      debugShowCheckedModeBanner: false,
    );
  }
}

class SliderPage extends StatefulWidget {
  const SliderPage({super.key});
  @override
  State<SliderPage> createState() => _SliderPageState();
}

class _SliderPageState extends State<SliderPage> {
  static const String baseUrl = 'http://192.168.100.101';
  List<int> angles = List.filled(5, 0);
  Timer? _debounce;

  void _onSliderChange(int index, double value) {
    setState(() => angles[index] = value.round());

    // Cancelar debounce previo si existe
    _debounce?.cancel();

    // Esperar 100 ms antes de enviar
    _debounce = Timer(const Duration(milliseconds: 100), () {
      _sendAngles();
    });
  }

  Future<void> _sendAngles() async {
    final query = List.generate(5, (i) => 'a${i + 1}=${angles[i]}').join('&');
    final url = Uri.parse('$baseUrl/sliders?$query');
    try {
      debugPrint('🌐 Enviando: $url');
      await http.get(url).timeout(const Duration(seconds: 1));
    } catch (e) {
      debugPrint('❌ Error al enviar: $e');
    }
  }

  @override
  void dispose() {
    _debounce?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Control de Ángulos')),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: ListView.builder(
          itemCount: 5,
          itemBuilder: (_, i) => Padding(
            padding: const EdgeInsets.symmetric(vertical: 10),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('Ángulo ${i + 1}: ${angles[i]}°'),
                Slider(
                  value: angles[i].toDouble(),
                  min: 0,
                  max: 180,
                  divisions: 180,
                  label: angles[i].toString(),
                  onChanged: (value) => _onSliderChange(i, value),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
