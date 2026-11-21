#!/usr/bin/env python3
"""
dbxdebug.py - drive the DOSBox-X debugger from Python (the 1:1 oracle).

DOSBox-X 2026.10+ has a `mcp_server = <port>` option in [dosbox]. Despite the
name, the emulator is the TCP *client*: it keeps connecting to 127.0.0.1:<port>,
and whoever listens there drives the debugger with a small line protocol:

    -> REQ <id> PING
    -> REQ <id> BREAK            (enter the debugger / pause)
    -> REQ <id> EXEC <command>   (any debugger command line, e.g. "SR", "BP 0546:0010";
                                  "RUN" resumes emulation after BREAK / a breakpoint)
    <- BEGIN <id> OK|ERR
       ...output lines...
       END <id>

With tools/dosbox/ist.conf, IST.EXE's image is loaded at paragraph 0x0824
(runtime segment = flat segment + 0x824 for resident code and DGROUP).

Usage as a library:
    with Debugger(port=7777) as d:      # call before/while starting DOSBox-X
        d.ping(); d.brk(); print(d.exec('SR'))
"""
import socket
import sys


class Debugger:
    def __init__(self, port=7777, accept_timeout=60.0, io_timeout=30.0):
        self.port = port
        self.accept_timeout = accept_timeout
        self.io_timeout = io_timeout
        self._id = 0
        self.sock = None
        self.buf = b''

    def __enter__(self):
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(('127.0.0.1', self.port))
        srv.listen(1)
        srv.settimeout(self.accept_timeout)
        self.srv = srv
        return self

    def wait_connect(self):
        self.sock, _ = self.srv.accept()
        self.sock.settimeout(self.io_timeout)
        return self

    def __exit__(self, *exc):
        for s in (self.sock, self.srv):
            try:
                s and s.close()
            except OSError:
                pass

    def _line(self):
        while b'\n' not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError('DOSBox-X closed the connection')
            self.buf += chunk
        line, self.buf = self.buf.split(b'\n', 1)
        return line.decode('latin-1').rstrip('\r')

    def request(self, verb, arg=None):
        if self.sock is None:
            self.wait_connect()
        self._id += 1
        rid = self._id
        msg = f'REQ {rid} {verb}' + (f' {arg}' if arg else '') + '\n'
        self.sock.sendall(msg.encode('latin-1'))
        # skip anything until our BEGIN
        while True:
            head = self._line()
            if head.startswith(f'BEGIN {rid} '):
                break
        status = head.split()[2]
        out = []
        while True:
            line = self._line()
            if line == f'END {rid}':
                break
            out.append(line)
        if status != 'OK':
            raise RuntimeError(f'{verb} {arg}: ' + '\n'.join(out))
        return '\n'.join(out)

    def ping(self):
        return self.request('PING')

    def brk(self):
        return self.request('BREAK')

    def exec(self, cmd):
        return self.request('EXEC', cmd)

    def run(self):
        return self.exec('RUN')

    def regs(self, *names):
        """Return {name: int} for registers via EV (e.g. regs('CS','IP','DS'))."""
        out = self.exec('EV ' + ' '.join(names)).splitlines()[-1].split()
        return {n: int(v, 16) for n, v in zip(names, out)}


if __name__ == '__main__':
    # interactive: python dbxdebug.py [port]  then type debugger commands
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 7777
    with Debugger(port) as d:
        print('waiting for DOSBox-X ...', flush=True)
        d.wait_connect()
        print(d.ping())
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                if line.upper() in ('PING', 'BREAK'):
                    print(d.request(line.upper()))
                else:
                    print(d.exec(line))
            except Exception as e:
                print('ERROR:', e)
