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
	# Кнопка "Создать комнату" ведёт НЕ на список комнат (как "Выбрать комнату"),
	# а открывает сцену ввода имени новой комнаты. После подтверждения шлём backend
	# {"type":"create_room","name":...}; когда сервер ответит
	# {"type":"room_created","id":...} (сигнал Net.room_created), сцена room_list
	# зайдёт в комнату и откроет сцену поля.
	var dialog := $CreateRoomDialog
	dialog.popup_centered()
	var edit: LineEdit = dialog.find_child("NameEdit", true, false)
	if edit and edit.text.strip_edges().is_empty():
		edit.text = "Комната %d" % (randi() % 1000)

func _on_create_dialog_confirmed() -> void:
	var dialog := $CreateRoomDialog
	var edit: LineEdit = dialog.find_child("NameEdit", true, false)
	var room_name := edit.text.strip_edges() if edit else ""
	if room_name.is_empty():
		room_name = "Комната %d" % (randi() % 1000)
	GameState.pending_room_name = room_name
	Net.server_error.connect(_on_net_error)
	if not Net.is_connected_to_server():
		# Подключение асинхронное — create_room отправим после connect (см. Net.create_room_deferred).
		Net.create_room_deferred(room_name)
		Net.connect_to_server(GameConfig.SERVER_HOST, GameConfig.SERVER_PORT)
	else:
		Net.create_room(room_name)
	get_tree().change_scene_to_file("res://scenes/room_list.tscn")

func _on_net_error(msg: String) -> void:
	push_warning("backend: " + msg)

func _on_local_mp_pressed() -> void:
	# Сообщаем ноде поля, что режим игры — локальный мультиплеер, и выбираем размер поля.
	var idx := size_option.selected
	GameState.start_local_mp(GameConfig.BOARD_SIZES[max(idx, 0)])
