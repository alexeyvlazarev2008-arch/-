# Сцена выбора имени и класса игрока (показывается при старте, если identity ещё нет).
extends Control

@onready var name_edit: LineEdit = $Panel/VBox/NameRow/NameEdit
@onready var class_opt: OptionButton = $Panel/VBox/ClassRow/ClassOption
@onready var ok_button: Button = $Panel/VBox/OkButton
@onready var status: Label = $Panel/VBox/Status

var _next_scene := "res://scenes/main_menu.tscn"

func _ready() -> void:
	for i in GameState.CLASSES.size():
	    class_opt.add_item(GameState.CLASSES[i])
	class_opt.select(0)
	ok_button.pressed.connect(_on_ok)
	if not GameState.player_name.is_empty():
	    name_edit.text = GameState.player_name
	status.text = "One Time ID будет выдан сервером при подключении."

func set_return_scene(scene_path: String) -> void:
	_next_scene = scene_path

func _on_ok() -> void:
	var nick := name_edit.text.strip_edges()
	if nick.is_empty():
	    status.text = "Введите имя игрока."
	    return
	GameState.choose_identity(nick, GameState.CLASSES[class_opt.selected])
	get_tree().change_scene_to_file(_next_scene)
