import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

void main() => runApp(const MyApp());

class MyApp extends StatelessWidget {
  const MyApp({super.key});
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Enviar Mensajes',
      home: const MessageSender(),
      debugShowCheckedModeBanner: false,
    );
  }
}

class MessageSender extends StatefulWidget {
  const MessageSender({super.key});
  @override
  State<MessageSender> createState() => _MessageSenderState();
}

class _MessageSenderState extends State<MessageSender> {
  final TextEditingController _controller = TextEditingController();
  static const String baseUrl = 'http://192.168.43.209';

  Future<void> sendMessage() async {
    final message = _controller.text.trim();
    if (message.isEmpty) return;

    final url = Uri.parse('$baseUrl/msg?msg=${Uri.encodeComponent(message)}');
    try {
      final response = await http.get(url).timeout(const Duration(seconds: 2));
      if (response.statusCode == 200) {
        debugPrint("✅ Enviado: $message");
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Mensaje enviado correctamente')),
        );
      } else {
        debugPrint("❌ Error al enviar mensaje");
      }
    } catch (e) {
      debugPrint('❌ Excepción: $e');
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Enviar Mensajes a ESP32')),
      body: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          children: [
            TextField(
              controller: _controller,
              decoration: const InputDecoration(
                labelText: 'Mensaje',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 20),
            ElevatedButton(
              onPressed: sendMessage,
              child: const Text('ENVIAR'),
            ),
          ],
        ),
      ),
    );
  }
}
