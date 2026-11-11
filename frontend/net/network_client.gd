# Клиент C++-сервера (StreamPeerTCP, newline-delimited JSON).
extends Node

signal connected_to_server()
signal connection_failed()
signal room_created(id: String)
signal rooms_updated(ids: PackedStringArray)
signal rooms_status_updated(rooms: Array)
signal joined_room(state: Dictionary)
signal state_received(state: Dictionary)
signal server_error(msg: String)
signal hello_received(token: String)
signal room_status_updated(status: Dictionary)
signal identity_ok()

var _peer := StreamPeerTCP.new()
var _host := ""
var _port := 0
var _connected := false
var _buf := PackedByteArray()
var _warned_web := false
var last_created_room := ""
var last_state := {}
# One Time ID: выдаётся сервером при подключении, одноразовый —
# каждый ответ сервера содержит свежий токен для следующего запроса.
var otid := ""

func _next_obj(obj: Dictionary) -> Dictionary:
	var o := obj.duplicate()
	if not otid.is_empty():
		o["token"] = otid
	return o

func connect_to_server(host: String, port: int) -> bool:
	_host = host; _port = port
	_buf.clear()
	_connected = false
	var err := _peer.connect_to_host(host, port)
	set_process(true)
	return err == OK

func is_connected_to_server() -> bool:
	return _connected

func _process(_dt: float) -> void:
	# Web-экспорт: браузер не даёт голый TCP — онлайн требует WebSocket-сервера.
	if DisplayServer.get_name() == "web":
		if not _warned_web:
			_warned_web = true
			server_error.emit("Онлайн-режим в веб-экспорте требует WebSocket-сервер (пока доступен только локальный мультиплеер)")
		set_process(false)
		return
	var st := _peer.get_status()
	if st == StreamPeerTCP.STATUS_CONNECTING:
		return
	if st != StreamPeerTCP.STATUS_CONNECTED:
		if _connected or not _warned_web:
			connection_failed.emit()
		set_process(false)
		return
	if not _connected:
		_connected = true
		connected_to_server.emit()
	var avail := _peer.get_available_bytes()
	while avail > 0:
		var data := _peer.get_data(avail)
		if data[0] != OK: break
		_handle_lines(data[1])
		avail = _peer.get_available_bytes()

func _handle_lines(chunk: PackedByteArray) -> void:
	_buf.append_array(chunk)
	while true:
		var nl := _buf.find(10)  # \n
		if nl < 0: break
		var line := _buf.slice(0, nl).get_string_from_utf8()
		_buf = _buf.slice(nl + 1)
		# line может быть битой (обрывок на границе TCP-пакета) или невалидным
		# JSON — parse_string вернёт null. Проверяем тип до обращения к словарю.
		if line.strip_edges().is_empty():
			continue
		var parsed = JSON.parse_string(line)
		if not (parsed is Dictionary):
			push_warning("NetworkClient: не удалось разобрать строку сервера: %s" % line)
			continue
		if true:
			# One Time ID: сервер в каждом ответе присылает свежий токен.
			if parsed.has("token"):
				otid = parsed["token"]
			match parsed.get("type", ""):
				"hello":
					otid = parsed.get("token", "")
					hello_received.emit(otid)
				"room_status", "player_joined", "player_left":
					room_status_updated.emit(parsed.get("status", {}))
				"identity_ok":
					identity_ok.emit()
				"room_created":
					last_created_room = parsed.get("id", "")
					room_created.emit(last_created_room)
				"rooms":
						var rs: Array = parsed.get("rooms", [])
						rooms_status_updated.emit(rs)
						var ids := PackedStringArray()
						for r in rs:
							ids.append(str(r.get("id", "")))
						rooms_updated.emit(ids)
				"joined":
					last_state = parsed.get("state", {})
					joined_room.emit(last_state)
				"state":
					last_state = parsed.get("state", {})
					state_received.emit(last_state)
				"error": server_error.emit(parsed.get("msg", ""))

func _send(obj: Dictionary) -> void:
	_peer.put_data((JSON.stringify(_next_obj(obj)) + "\n").to_utf8_buffer())

func create_room(name: String) -> void:
	_send({"type": "create_room", "name": name, "nickname": GameState.player_name, "class": GameState.player_class})
func list_rooms() -> void: _send({"type": "list_rooms"})
func join_room(id: String, player: String) -> void:
	_send({"type": "join", "room": id, "player": player, "nickname": GameState.player_name, "class": GameState.player_class})
func set_identity(nickname: String, char_class: String) -> void:
	_send({"type": "set_identity", "nickname": nickname, "class": char_class})
func request_state() -> void: _send({"type": "state"})
func send_move(dx: int, dy: int) -> void: _send({"type": "move", "dx": dx, "dy": dy})
func send_end_turn() -> void: _send({"type": "end_turn"})
func send_card(card: String) -> void: _send({"type": "card", "card": card})
func start_game() -> void: _send({"type": "start"})
