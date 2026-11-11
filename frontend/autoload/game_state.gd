# Autoload singleton: передаёт данные между сценами (режим игры, размер поля).
extends Node

var mode: int = GameConfig.GameMode.LOCAL_MP
var board_size: int = GameConfig.DEFAULT_BOARD_SIZE
var player_names: PackedStringArray = ["Гном 1", "Гном 2", "Гном 3", "Гном 4"]

func start_local_mp(size: int = GameConfig.DEFAULT_BOARD_SIZE) -> void:
	mode = GameConfig.GameMode.LOCAL_MP
	board_size = size
	get_tree().change_scene_to_file("res://scenes/board.tscn")

# --- Онлайн-режим (связь с C++ backend) ---
var room_id: String = ""
var player_name: String = "Гном"
var player_class: String = "dwarf"
const CLASSES: Array[String] = ["dwarf", "goblin_scout"]

func has_identity() -> bool:
	return not player_name.is_empty() and not player_class.is_empty()

func choose_identity(nickname: String, char_class: String) -> void:
	player_name = nickname
	player_class = char_class
	if Net != null and Net.is_connected_to_server():
		Net.set_identity(nickname, char_class)
var online_state: Dictionary = {}

func start_online(size: int = GameConfig.DEFAULT_BOARD_SIZE) -> void:
	mode = GameConfig.GameMode.ONLINE
	board_size = size
	get_tree().change_scene_to_file("res://scenes/board.tscn")
