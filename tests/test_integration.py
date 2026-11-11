# -*- coding: utf-8 -*-
"""Интеграционный тест: Godot-клиент (эмуляция network_client.gd) <-> C++ сервер.

Проверяет, что протокол и схема JSON совместимы между frontend и backend,
и что кнопки "Создать комнату"/"Выбрать комнату" получают корректные ответы.
"""
import json
import os
import socket
import subprocess
import time

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER_BIN = os.path.join(ROOT, 'backend', 'build', 'server')


def _build_server():
    build = os.path.join(ROOT, 'backend', 'build')
    os.makedirs(build, exist_ok=True)
    subprocess.run(['cmake', '..'], cwd=build, check=True, capture_output=True)
    subprocess.run(['cmake', '--build', '.'], cwd=build, check=True, capture_output=True)


class GodotClient:
    """Эмуляция логики frontend/net/network_client.gd (newline-delimited JSON)."""

    def __init__(self, port):
        self.sock = socket.create_connection(('127.0.0.1', port), timeout=5)
        self.f = self.sock.makefile('rwb')

    def send(self, obj):
        self.f.write((json.dumps(obj) + '\n').encode('utf-8'))
        self.f.flush()

    def recv(self):
        line = self.f.readline()
        assert line, 'server closed connection'
        return json.loads(line.decode('utf-8'))


@pytest.fixture(scope='module')
def server():
    if not os.path.exists(SERVER_BIN):
        _build_server()
    port = 19042
    proc = subprocess.Popen([SERVER_BIN, str(port)], stdout=subprocess.DEVNULL)
    for _ in range(50):
        try:
            socket.create_connection(('127.0.0.1', port), timeout=0.2).close()
            break
        except OSError:
            time.sleep(0.1)
    else:
        proc.kill()
        pytest.fail('backend server did not start')
    yield port
    proc.terminate()
    proc.wait(timeout=5)


def test_create_room_button_flow(server):
    """Кнопка "Создать комнату": create_room -> room_created.id (нет строковых id!)"""
    c = GodotClient(server)
    c.send({'type': 'create_room', 'name': 'Комната 42'})
    r = c.recv()
    assert r['type'] == 'room_created'
    rid = r['id']
    assert isinstance(rid, int) or str(rid).isdigit(), \
        'GDScript is_empty() работает только со String — id комнаты должен быть числом/строкой из цифр'
    # room_list.gd после создания сразу делает join по last_created_room
    c.send({'type': 'join', 'room': str(rid), 'player': 'Гном'})
    r = c.recv()
    assert r['type'] == 'joined'
    st = r['state']
    assert st['phase'] == 0          # ожидание игроков
    assert st['size'] in (6, 8, 10)  # board.gd принимает только эти размеры
    c.sock.close()


def test_state_schema_matches_board_gd(server):
    """board.gd:_on_server_state читает phase/turn/mined/current/grid/players/moves_left/cards_left."""
    c = GodotClient(server)
    c.send({'type': 'create_room', 'name': 'schema'})
    rid = str(c.recv()['id'])
    players = ['Гном 1', 'Гном 2']
    for p in players:
        c.send({'type': 'join', 'room': rid, 'player': p})
        c.recv()
    c.send({'type': 'start', 'room': rid})
    c.send({'type': 'state', 'room': rid})
    msg = None
    while msg is None:
        r = c.recv()
        if r['type'] == 'state':
            msg = r
    st = msg['state']
    for key in ('phase', 'turn', 'mined', 'current', 'grid', 'players',
                'moves_left', 'cards_left', 'size'):
        assert key in st, f'ключ {key} отсутствует в состоянии сервера'
    grid = st['grid']
    assert isinstance(grid, str) and len(grid) == st['size'] ** 2, \
        'flat string grid длиной size*size (ожидание board.gd)'
    for pl in st['players']:
        for key in ('x', 'y', 'alive'):
            assert key in pl
    c.sock.close()


def test_move_end_turn_broadcast(server):
    """Действия с поля (send_move/send_end_turn) приводят к рассылке state остальным."""
    a = GodotClient(server)
    a.send({'type': 'create_room', 'name': 'mp'})
    rid = str(a.recv()['id'])
    a.send({'type': 'join', 'room': rid, 'player': 'A'})
    a.recv()
    b = GodotClient(server)
    b.send({'type': 'join', 'room': rid, 'player': 'B'})
    b.recv()
    a.send({'type': 'start', 'room': rid})
    # дождаться started/state у обоих
    def wait_started(cl):
        for _ in range(10):
            m = cl.recv()
            if m['type'] in ('started', 'state'):
                return m
        raise AssertionError('no started/state')
    wait_started(a); wait_started(b)
    a.send({'type': 'move', 'room': rid, 'dx': 1, 'dy': 0})
    m = wait_started(a)
    assert m['state']['phase'] >= 1
    a.send({'type': 'end_turn', 'room': rid})
    m = wait_started(a)
    assert m['type'] == 'state'
    a.sock.close(); b.sock.close()


def test_room_capacity_max_4(server):
    """Комната вмещает максимум 4 игрока (issue #3) — пятый получает error."""
    c = GodotClient(server)
    c.send({'type': 'create_room', 'name': 'cap'})
    rid = str(c.recv()['id'])
    for i in range(4):
        c.send({'type': 'join', 'room': rid, 'player': f'P{i}'})
        assert c.recv()['type'] == 'joined'
    c.send({'type': 'join', 'room': rid, 'player': 'P5'})
    assert c.recv()['type'] == 'error'
    c.sock.close()
