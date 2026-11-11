class_name Dwarf
extends GameCharacter
## Гном — игровой персонаж игрока (наследник GameCharacter, issue #3).

const COLORS: Array[Color] = [
	Color(0.90, 0.45, 0.20),  # игрок 1 — рыжий
	Color(0.25, 0.65, 0.90),  # игрок 2 — синий
	Color(0.95, 0.80, 0.25),  # игрок 3 — жёлтый
	Color(0.55, 0.85, 0.45),  # игрок 4 — зелёный
]

func _ready() -> void:
	if display_name == "Персонаж":
		display_name = "Гном %d" % (id + 1)
	color = COLORS[id % COLORS.size()]
	queue_redraw()

@export var id: int = 0
