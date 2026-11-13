class_name Goblin
extends Character
## Гоблин-лазутчик — появляется на зелёных клетках после GOBLIN_START_TURN.

func _ready() -> void:
	color = Color(0.35, 0.75, 0.30)
	display_name = "Гоблин"
	queue_redraw()
