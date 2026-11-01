#!/usr/bin/env python3
"""HTTP, permissions, fragmentation and stdio integration; no radio hardware."""
import http.client
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import unittest


class McpTransport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            cls.port = sock.getsockname()[1]
        cls.token = secrets.token_hex(32)
        cls.env = dict(os.environ, JTDX_MCP_TOKEN=cls.token,
                       JTDX_MCP_URL=f'http://127.0.0.1:{cls.port}/mcp')
        cls.proc = subprocess.Popen([sys.argv[1], str(cls.port)], env=cls.env, stdout=subprocess.PIPE)
        assert cls.proc.stdout.readline().strip() == b'ready'

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait(timeout=5)
        cls.proc.stdout.close()

    def rpc(self, method='ping', params=None, ident=1, **overrides):
        body = {'jsonrpc': '2.0', 'method': method}
        if ident is not None:
            body['id'] = ident
        if params is not None:
            body['params'] = params
        return self.request(json.dumps(body), **overrides)

    def request(self, body, **overrides):
        headers = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream',
                   'Authorization': 'Bearer ' + self.token}
        headers.update(overrides)
        conn = http.client.HTTPConnection('127.0.0.1', self.port, timeout=3)
        conn.request('POST', '/mcp', body, headers)
        response = conn.getresponse()
        status, data = response.status, response.read()
        conn.close()
        return status, json.loads(data) if data else None

    def test_initialize_and_catalog(self):
        status, data = self.rpc('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                                              'clientInfo': {'name': 'test', 'version': '1'}})
        self.assertEqual(status, 200)
        self.assertEqual(data['result']['serverInfo']['name'], 'JTDX-F')
        self.assertEqual(len(self.rpc('tools/list')[1]['result']['tools']), 22)
        self.assertEqual(len(self.rpc('resources/list')[1]['result']['resources']), 5)
        self.assertEqual(self.rpc('resources/read', {'uri': 'jtdx://status'})[0], 200)
        self.assertEqual(self.rpc('prompts/get', {'name': 'operate_ft8'})[0], 200)

    def test_authentication_and_origin(self):
        self.assertEqual(self.rpc(Authorization='Bearer wrong')[0], 401)
        self.assertEqual(self.rpc(Host=f'evil.invalid:{self.port}')[0], 403)
        self.assertEqual(self.rpc(Origin='https://evil.invalid')[0], 403)
        self.assertEqual(self.rpc(**{'MCP-Protocol-Version': '1900-01-01'})[0], 400)

    def test_protocol_errors(self):
        self.assertEqual(self.request('{')[1]['error']['code'], -32700)
        self.assertEqual(self.request('[]')[1]['error']['code'], -32600)
        self.assertEqual(self.rpc('missing')[1]['error']['code'], -32601)
        self.assertEqual(self.rpc('tools/call', {'name': 'set_options', 'arguments': {'watchdog_minutes': 1000}})[1]['error']['code'], -32602)
        self.assertEqual(self.rpc('notifications/initialized', ident=None), (202, None))
        self.assertEqual(self.request('x' * 65537)[0], 413)

    def test_transmit_permissions(self):
        self.assertTrue(self.rpc('tools/call', {'name': 'start_tx'})[1]['result']['isError'])
        self.assertFalse(self.rpc('tools/call', {'name': 'stop_tx'})[1]['result']['isError'])

    def test_fragmented_request(self):
        body = b'{"jsonrpc":"2.0","id":5,"method":"ping"}'
        request = (f'POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:{self.port}\r\n'
                   f'Authorization: Bearer {self.token}\r\nContent-Type: application/json\r\n'
                   f'Content-Length: {len(body)}\r\n\r\n').encode() + body
        with socket.create_connection(('127.0.0.1', self.port), timeout=3) as sock:
            for i in range(0, len(request), 7):
                sock.sendall(request[i:i+7])
            response = http.client.HTTPResponse(sock)
            response.begin()
            self.assertEqual(json.loads(response.read())['id'], 5)

    def test_stdio_bridge(self):
        bridge = Path(__file__).resolve().parents[1] / 'tools/jtdx_mcp_bridge.py'
        requests = [
            {'jsonrpc': '2.0', 'id': 'init', 'method': 'initialize', 'params': {
                'protocolVersion': '2025-11-25', 'capabilities': {}, 'clientInfo': {'name': 'bridge-test', 'version': '1'}}},
            {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
            {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'},
        ]
        result = subprocess.run([sys.executable, str(bridge)], input='\n'.join(map(json.dumps, requests))+'\n',
                                text=True, capture_output=True, env=self.env, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([line['id'] for line in lines], ['init', 2])
        self.assertEqual(len(lines[1]['result']['tools']), 22)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]], verbosity=2)
