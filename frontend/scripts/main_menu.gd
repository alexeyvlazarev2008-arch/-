# Главный меню (issue #3): кнопки выбора комнат, создания комнаты и локального мультиплеера.
extends Control

@onready var rooms_button: Button = $VBox/RoomsButton
@onready var create_button: Button = $VBox/CreateRoomButton
@onready var local_mp_button: Button = $VBox/LocalMpButton
@onready var size_option: OptionButton = $VBox/SizeRow/SizeOption

func _ready() -> void:
	# Если имя/класс ещё не выбраны — сначала сцена выбора identity.
	if not GameState.has_identity():
		get_tree().change_scene_to_file("res://scenes/identity.tscn")
		return
	rooms_button.pressed.connect(func(): get_tree().change_scene_to_file("res://scenes/room_list.tscn"))
	create_button.pressed.connect(_on_create_room_pressed)
	local_mp_button.pressed.connect(_on_local_mp_pressed)
	for s in GameConfig.BOARD_SIZES:
		size_option.add_item("%dx%d" % [s, s])
	size_option.select(GameConfig.BOARD_SIZES.find(GameConfig.DEFAULT_BOARD_SIZE))

func _on_create_room_pressed() -> void:
	# Кнопка "Создать комнату": шлём C++ backend {"type":"create_room"},
	# сервер отвечает {"type":"room_created","id":...} — id сохраняем в GameState
	# через autoload Net и открываем список комнат.
	Net.server_error.connect(_on_net_error)
	if not Net.is_connected_to_server():
		Net.connected_to_server.connect(_send_create_room)
		Net.connect_to_server(GameConfig.SERVER_HOST, GameConfig.SERVER_PORT)
	else:
		_send_create_room()
	get_tree().change_scene_to_file("res://scenes/room_list.tscn")

func _send_create_room() -> void:
	Net.create_room("Комната %d" % (randi() % 100))

func _on_net_error(msg: String) -> void:
	push_warning("backend: " + msg)

func _on_local_mp_pressed() -> void:
	# Сообщаем ноде поля, что режим игры — локальный мультиплеер, и выбираем размер поля.
	var idx := size_option.selected
	GameState.start_local_mp(GameConfig.BOARD_SIZES[max(idx, 0)])
