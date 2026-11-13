extends Node2D
## Сцена игрового поля (issue #3). Квадратное поле 6x6 / 8x8 / 10x10,
## пошаговый локальный мультиплеер: за ход n шагов и x карт, затем ход
## переходит к следующему игроку (или по кнопке "Закончить ход").

const CELL := 64.0
const TILE_COLORS := {
	".": Color(0.16, 0.14, 0.18),   # пол шахты (тёмный фон-плейсхолдер)
	"#": Color(0.45, 0.38, 0.30),   # стена
	"o": Color(0.75, 0.72, 0.68),   # камень строительный
	"g": Color(0.60, 0.35, 0.80),   # драгоценный камень
	"x": Color(0.72, 0.55, 0.30),   # коробка
	"m": Color(0.85, 0.30, 0.35),   # гриб (проход закрыт)
	"c": Color(0.30, 0.85, 0.85),   # кристалл (проход закрыт)
	"Y": Color(0.85, 0.78, 0.25),   # жёлтая клетка: спавн камней
	"G": Color(0.30, 0.65, 0.30),   # зелёная клетка: лазутчики-гоблины
}
const EXIT_COLOR := Color(0.20, 0.85, 0.60)  # выходы с четырёх сторон

@export var size: int = GameConfig.DEFAULT_BOARD_SIZE

var grid: Array[String] = []
var players: Array[Dwarf] = []
var current: int = 0
var turn_number: int = 1
var mined_stones: int = 0
var goblins: Array[Goblin] = []
var game_over: bool = false
var _is_online: bool = false

@onready var hud: Label = $HUD
@onready var end_turn_button: Button = $UI/EndTurnButton
@onready var card_button: Button = $UI/CardButton
@onready var menu_button: Button = $UI/MenuButton

func _ready() -> void:
	size = GameState.board_size if GameState.board_size in GameConfig.BOARD_SIZES else size
	_is_online = GameState.mode == GameConfig.GameMode.ONLINE
	_generate_map()
	_spawn_players()
	end_turn_button.pressed.connect(_on_end_turn_pressed)
	card_button.pressed.connect(_on_card_pressed)
	menu_button.pressed.connect(func(): get_tree().change_scene_to_file("res://scenes/main_menu.tscn"))
	_set_cards_enabled()
	if _is_online:
		# Онлайн: состояние игры живёт на C++ backend, рисуем его и шлём действия.
		Net.state_received.connect(_on_server_state)
		Net.joined_room.connect(_on_server_state)
		if not Net.last_state.is_empty():
			_on_server_state(Net.last_state)  # состояние из ответа на join
		else:
			Net.request_state()
	_update_hud()

# --- генерация карты: стены + выходы со всех четырёх сторон (concept issue #2)
func _generate_map() -> void:
	grid.clear()
	for y in size:
		var row := ""
		for x in size:
			var ch := "."
			if randf() < 0.18 and not (x < 2 and y < 2):
				ch = "#"
			elif randf() < 0.06:
				ch = "o"
			row += ch
		grid.append(row)
	# жёлтые клетки спавна камней и зелёные клетки гоблинов
	_set_cell(size - 1, size / 2, "Y")
	_set_cell(0, size / 2 - 1, "G")
	# выходы: гарантированные проходы в середине каждой из четырёх стен-кромок
	var m := size / 2
	_set_cell(m, 0, "."); _set_cell(m, 1, ".")       # верхний выход (арка)
	_set_cell(m, size - 1, "."); _set_cell(m, size - 2, ".")  # нижний
	_set_cell(0, m, "."); _set_cell(1, m, ".")       # левый
	_set_cell(size - 1, m, "."); _set_cell(size - 2, m, ".")  # правый
	# камни у выходов — их удобно выталкивать за поле (добыча)
	_set_cell(m, 2, "o"); _set_cell(size - 3, m, "o")

func _set_cell(x: int, y: int, ch: String) -> void:
	var row := grid[y]
	assert(x < row.length())
	grid[y] = row.substr(0, x) + ch + row.substr(x + 1)

func cell_char(x: int, y: int) -> String:
	if x < 0 or y < 0 or x >= size or y >= size:
		return ""     # за пределами поля — клетка "выход"
	return String(grid[y][x])

func cell_to_pixel(c: Vector2i) -> Vector2:
	return Vector2(c.x * CELL + CELL / 2.0, c.y * CELL + CELL / 2.0) + Vector2(64, 64)

func _spawn_players() -> void:
	var corners := [Vector2i(0, 0), Vector2i(size - 1, size - 1),
		Vector2i(0, size - 1), Vector2i(size - 1, 0)]
	for i in GameConfig.MAX_PLAYERS:
		var d := Dwarf.new()
		d.id = i
		d.board = self
		add_child(d)
		d.place_at(corners[i].x, corners[i].y)
		players.append(d)

# --- ввод: текущий игрок двигается WASD/стрелками ---
func _unhandled_input(event: InputEvent) -> void:
	if game_over or not (event is InputEventKey and event.pressed and not event.echo):
		return
	var dir := Vector2i.ZERO
	match event.keycode:
		KEY_W, KEY_UP: dir = Vector2i(0, -1)
		KEY_S, KEY_DOWN: dir = Vector2i(0, 1)
		KEY_A, KEY_LEFT: dir = Vector2i(-1, 0)
		KEY_D, KEY_RIGHT: dir = Vector2i(1, 0)
		_: return
	try_move(dir)

func try_move(dir: Vector2i) -> bool:
	if _is_online:
		Net.send_move(dir.x, dir.y)
		return true
	var p := players[current]
	if p.moves_left <= 0:
		return false
	var target := p.cell + dir
	var ch := cell_char(target.x, target.y)
	if ch == "":  # шаг за пределы поля запрещён (только толкание выносит камни)
		return false
	# толкаемые объекты: камень, коробка, драгоценный камень
	if ch in ["o", "g", "x"]:
		var beyond := target + dir
		var bch := cell_char(beyond.x, beyond.y)
		if bch == "":  # вытолкнут за край — камень добыт!
			_set_cell(target.x, target.y, ".")
			if ch == "o":
				mined_stones += 1
			_check_win()
		elif bch == ".":
			_set_cell(beyond.x, beyond.y, ch)
			_set_cell(target.x, target.y, ".")
		else:
			return false
	elif ch in ["#", "m", "c"]:
		return false  # стена/гриб/кристалл — прохода нет
	elif _player_at(target):
		return false
	p.moves_left -= 1
	p.place_at(target.x, target.y)
	if p.moves_left <= 0:
		_end_turn()
	else:
		_update_hud()
	return true

func _player_at(c: Vector2i) -> Dwarf:
	for p in players:
		if p.alive and p.cell == c:
			return p
	return null

# --- карты (теги описаны в cards/tags.md; эффект первой версии — прыжок) ---
func use_jump_card(dist: int = 2) -> void:
	var p := players[current]
	if p.cards_left <= 0:
		return
	p.cards_left -= 1
	# прыжок: перемещение на dist клеток вправо с игнорированием препятствий
	var nx := mini(p.cell.x + dist, size - 1)
	while cell_char(nx, p.cell.y) in ["#", "o", "g", "x", "m", "c"] or _player_at(Vector2i(nx, p.cell.y)):
		nx += 1
	if nx >= size:
		nx = p.cell.x
	p.place_at(nx, p.cell.y)
	_set_cards_enabled()
	_update_hud()

func _set_cards_enabled() -> void:
	card_button.disabled = players[current].cards_left <= 0

# --- переход хода ---
func _on_end_turn_pressed() -> void:
	if _is_online:
		Net.send_end_turn()
		return
	_end_turn()

func _on_card_pressed() -> void:
	if _is_online:
		Net.send_card("jump_2")
		return
	use_jump_card(2)

# --- онлайн: применение состояния от сервера (backend — источник истины) ---
func _on_server_state(state: Dictionary) -> void:
	if state.is_empty():
		return
	GameState.online_state = state
	var w: int = int(state.get("size", size))
	if w != size:
		size = w
	var flat := String(state.get("grid", ""))
	if flat.length() == size * size:
		grid.clear()
		for y in size:
			grid.append(flat.substr(y * size, size))
	turn_number = int(state.get("turn", turn_number))
	mined_stones = int(state.get("mined", mined_stones))
	game_over = int(state.get("phase", 0)) >= 2  # LOST/WON на backend
	var plist: Array = state.get("players", [])
	if not plist.is_empty():
		# сервер отдаёт плоский grid + список игроков; активный игрок — state.current
		current = clampi(int(state.get("current", current)), 0, players.size() - 1)
		for i in mini(players.size(), plist.size()):
			var pd: Dictionary = plist[i]
			players[i].place_at(int(pd.get("x", 0)), int(pd.get("y", 0)))
			players[i].alive = bool(pd.get("alive", true))
		players[current].moves_left = int(state.get("moves_left", GameConfig.MOVES_PER_TURN))
		players[current].cards_left = int(state.get("cards_left", GameConfig.CARDS_PER_TURN))
	_set_cards_enabled()
	_update_hud("Синхронизировано с сервером")

func _end_turn() -> void:
	# следующий живой игрок
	for i in range(1, players.size() + 1):
		var idx := (current + i) % players.size()
		if players[idx].alive:
			current = idx
			break
	if current == 0:
		turn_number += 1
		if turn_number > GameConfig.TOTAL_TURNS:
			_lose("Нашествие началось, крепость не построена!")
			return
		if turn_number >= GameConfig.GOBLIN_START_TURN:
			_spawn_goblin()
	for p in players:
		p.reset_turn_resources()
	_set_cards_enabled()
	_update_hud()

func _spawn_goblin() -> void:
	var spots: Array[Vector2i] = []
	for y in size:
		for x in size:
			if grid[y][x] == "G":
				spots.append(Vector2i(x, y))
	if spots.is_empty():
		return
	var g := Goblin.new()
	g.board = self
	add_child(g)
	g.place_at(spots[randi() % spots.size()].x, spots[randi() % spots.size()].y)
	goblins.append(g)

func _check_win() -> void:
	if mined_stones >= GameConfig.STONES_TO_BUILD:
		game_over = true
		_update_hud("КРЕПОСТЬ ПОСТРОЕНА! Победа!")

func _lose(msg: String) -> void:
	game_over = true
	_update_hud(msg)

func _update_hud(msg: String = "") -> void:
	if msg != "":
		hud.text = msg
		return
	var p := players[current]
	hud.text = "Ход %d/%d | Игрок: %s | Шагов: %d | Карт: %d | Камней: %d/%d" % [
		turn_number, GameConfig.TOTAL_TURNS, p.display_name,
		p.moves_left, p.cards_left, mined_stones, GameConfig.STONES_TO_BUILD]

# --- отрисовка фоновых тайлов (плейсхолдеры: градиент + цветные квадраты) ---
func _draw() -> void:
	# градиентный задник под полем — чтобы тайлы не сливались с окном
	for y in size:
		for x in size:
			var ch := grid[y][x]
			var col: Color = TILE_COLORS.get(ch, TILE_COLORS["."])
			var pos := Vector2(x * CELL, y * CELL) + Vector2(64, 64) - Vector2(CELL / 2.0, CELL / 2.0)
			draw_rect(Rect2(pos, Vector2(CELL - 4, CELL - 4)), col.lerp(Color.BLACK, y / float(size) * 0.25))
	# выходы — светящиеся арки по центру каждой стороны
	var m := size / 2
	for e in [Vector2(m * CELL + 32, 40), Vector2(m * CELL + 32, size * CELL + 88),
			Vector2(40, m * CELL + 64), Vector2(size * CELL + 88, m * CELL + 64)]:
		draw_circle(e, 14.0, EXIT_COLOR)
		draw_circle(e, 20.0, Color(EXIT_COLOR, 0.3))
