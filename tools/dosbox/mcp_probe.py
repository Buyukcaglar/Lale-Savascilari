"""Probe the DOSBox-X debugger MCP server (TCP, JSON-RPC 2.0)."""
import json, socket, sys, time
port = int(sys.argv[1]) if len(sys.argv) > 1 else 7777
for attempt in range(40):
    try:
        s = socket.create_connection(('127.0.0.1', port), timeout=5); break
    except OSError:
        time.sleep(0.5)
else:
    sys.exit('no server')
f = s.makefile('rwb')
def rpc(i, method, params=None):
    msg = {'jsonrpc': '2.0', 'id': i, 'method': method}
    if params is not None: msg['params'] = params
    f.write((json.dumps(msg) + '\n').encode()); f.flush()
    line = f.readline()
    return json.loads(line) if line else None
print(json.dumps(rpc(1, 'initialize', {'protocolVersion': '2025-06-18', 'capabilities': {},
      'clientInfo': {'name': 'probe', 'version': '0'}}), indent=1)[:1500])
f.write((json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}) + '\n').encode()); f.flush()
r = rpc(2, 'tools/list')
for t in r['result']['tools']:
    print('-', t['name'], ':', t.get('description', '')[:150].replace('\n', ' '))
    print('     params:', json.dumps(t.get('inputSchema', {}).get('properties', {}))[:300])
