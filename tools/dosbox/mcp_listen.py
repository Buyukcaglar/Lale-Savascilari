"""Accept the DOSBox-X debugger connection on the mcp_server port and probe
the protocol: send MCP initialize / tools/list and print the replies."""
import json, socket, sys
port = int(sys.argv[1]) if len(sys.argv) > 1 else 7777
framing = sys.argv[2] if len(sys.argv) > 2 else 'nl'
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(('127.0.0.1', port)); srv.listen(1); srv.settimeout(20)
c, a = srv.accept(); print('connected from', a, 'framing', framing, flush=True)
c.settimeout(4)

def send(obj):
    body = json.dumps(obj).encode()
    if framing == 'nl':
        c.sendall(body + b'\n')
    else:
        c.sendall(b'Content-Length: %d\r\n\r\n' % len(body) + body)

def recv_all():
    data = b''
    try:
        while True:
            b = c.recv(65536)
            if not b: break
            data += b
    except socket.timeout:
        pass
    return data

send({'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {
      'protocolVersion': '2025-06-18', 'capabilities': {},
      'clientInfo': {'name': 'lale-oracle', 'version': '0'}}})
print('init reply:', recv_all()[:2000], flush=True)
send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
send({'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'})
d = recv_all()
print('tools reply bytes:', len(d), flush=True)
try:
    txt = d.decode('utf-8', 'replace')
    j = json.loads(txt[txt.index('{'):].strip().splitlines()[0]) if framing == 'nl' else json.loads(txt[txt.index('{'):])
    for t in j['result']['tools']:
        print('-', t['name'], ':', t.get('description', '').replace('\n', ' ')[:200])
        print('     params:', json.dumps(t.get('inputSchema', {}).get('properties', {}))[:400])
except Exception as e:
    print('raw:', d[:3000], 'err', e)
