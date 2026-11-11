class_name Character
extends Node2D
## Базовый класс персонажа (issue #3). Хранит позицию на поле и цвет-плейсхолдер.

@export var cell: Vector2i = Vector2i.ZERO
@export var color: Color = Color.WHITE
@export var display_name: String = "Персонаж"

var board  # ссылка на Board (устанавливает сцена поля)
var moves_left: int = GameConfig.MOVES_PER_TURN
var cards_left: int = GameConfig.CARDS_PER_TURN
var alive: bool = true

func _draw() -> void:
	# Плейсхолдер: квадрат с тёмной обводкой — не сливается с фоном поля.
	var s := 56.0
	var rect := Rect2(-s / 2.0, -s / 2.0, s, s)
	draw_rect(rect.grow(4.0), Color(0.1, 0.1, 0.12))  # контур
	draw_rect(rect, color)
	draw_string(get_theme_default_font(), Vector2(-s / 2.0, -s / 2.0 - 8.0),
		display_name, HORIZONTAL_ALIGNMENT_CENTER, s * 2, 14, Color(0.9, 0.9, 0.95))

func place_at(x: int, y: int) -> void:
	cell = Vector2i(x, y)
	position = board.cell_to_pixel(cell) if board else Vector2.ZERO
	queue_redraw()

func reset_turn_resources() -> void:
	moves_left = GameConfig.MOVES_PER_TURN
	cards_left = GameConfig.CARDS_PER_TURN
