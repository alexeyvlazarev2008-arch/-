# Сцена списка доступных комнат для подключения (issue #3).
extends Control

@onready var list: VBoxContainer = $Panel/VBox/RoomList
@onready var back_button: Button = $Panel/VBox/BackButton
@onready var status: Label = $Panel/VBox/Status

var _net
var _last_rooms: Array = []

func _ready() -> void:
	back_button.pressed.connect(func(): get_tree().change_scene_to_file("res://scenes/main_menu.tscn"))
	_net = Net  # общий autoload-клиент (переживает смену сцен)
	_net.rooms_status_updated.connect(_show_rooms_status)
	_net.server_error.connect(_on_error)
	_net.connection_failed.connect(_on_conn_failed)
	_net.room_created.connect(_on_room_created)
	_net.joined_room.connect(_on_joined)
	_net.room_status_updated.connect(_on_room_status)
	if not _net.is_connected_to_server():
		_net.connected_to_server.connect(_after_connect)
		_net.connect_to_server(GameConfig.SERVER_HOST, GameConfig.SERVER_PORT)
	else:
		_after_connect()

func _after_connect() -> void:
	# Если в главном меню нажимали "Создать комнату" — заходим в неё сразу.
	if not _net.last_created_room.is_empty():
		_join(_net.last_created_room)
		return
	_net.list_rooms()

func _on_room_created(id: String) -> void:
	status.text = "Комната %s создана." % id
	_join(id)

func _on_joined(_state: Dictionary) -> void:
	GameState.start_online(GameConfig.DEFAULT_BOARD_SIZE)

func _on_conn_failed() -> void:
	status.text = "Сервер недоступен. Пока доступен только локальный мультиплеер."

# Живое обновление статуса комнаты (кол-во игроков и никнеймы) из сигналов backend.
func _on_room_status(st: Dictionary) -> void:
	if str(st.get("id", "")) != GameState.room_id:
		return
	status.text = _fmt_room(st)

func _show_rooms_status(rooms: Array) -> void:
	_last_rooms = rooms
	for c in list.get_children():
		c.queue_free()
	for r in rooms:
		var b := Button.new()
		b.text = _fmt_room(r)
		b.pressed.connect(_join.bind(str(r.get("id", ""))))
		list.add_child(b)
	if rooms.is_empty():
		status.text = "Комнат пока нет — создайте новую в главном меню."

func _fmt_room(r: Dictionary) -> String:
	var names: PackedStringArray = PackedStringArray()
	for m in r.get("members", []):
		var nick := String(m.get("nickname", ""))
		names.append(nick if not nick.is_empty() else "без имени")
	var n := int(r.get("players", 0))
	var mx := int(r.get("max", GameConfig.MAX_PLAYERS))
	return "%s (%d/%d): %s" % [r.get("name", "?"), n, mx, ", ".join(names)]

func _join(room_id: String) -> void:
	# Подключение к комнате: сервер добавляет игрока (макс. 4) и шлёт состояние.
	# Сцену поля открываем в _on_joined — когда backend подтвердил вход.
	GameState.room_id = room_id
	_net.join_room(room_id, GameState.player_name)

func _on_error(msg: String) -> void:
	status.text = "Ошибка сервера: " + msg
