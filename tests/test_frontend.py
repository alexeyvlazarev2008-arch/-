"""Tests for the Godot frontend project structure (issue #1/#3).

Validates that all required scenes/scripts/config exist and are well-formed.
Full runtime validation requires the Godot binary (see CI workflow).
"""
import re
from pathlib import Path

FRONTEND = Path(__file__).resolve().parent.parent / "frontend"


def test_project_godot_exists():
    text = (FRONTEND / "project.godot").read_text(encoding="utf-8")
    assert 'config/name=' in text
    assert 'run/main_scene="res://scenes/main_menu.tscn"' in text
    assert 'gl_compatibility' in text  # WebGL-friendly renderer


def test_config_file_has_turn_params():
    cfg = (FRONTEND / "game_config.gd").read_text(encoding="utf-8")
    for key in ("BOARD_SIZES", "MOVES_PER_TURN", "CARDS_PER_TURN", "MAX_PLAYERS"):
        assert key in cfg, f"{key} must be in game_config.gd"
    assert "[6, 8, 10]" in cfg or re.search(r"\[6,\s*8,\s*10\]", cfg)


def test_required_scenes_exist():
    for scene in ("main_menu.tscn", "board.tscn", "room_list.tscn", "dwarf.tscn"):
        p = FRONTEND / "scenes" / scene
        assert p.exists(), f"missing scene {scene}"
        text = p.read_text(encoding="utf-8")
        assert text.startswith("[gd_scene"), f"{scene} is not a valid scene header"


def test_character_class_hierarchy():
    char = (FRONTEND / "characters" / "character.gd").read_text(encoding="utf-8")
    dwarf = (FRONTEND / "characters" / "dwarf.gd").read_text(encoding="utf-8")
    goblin = (FRONTEND / "characters" / "goblin.gd").read_text(encoding="utf-8")
    assert "class_name GameCharacter" in char
    assert "extends Node2D" in char
    assert "class_name Dwarf" in dwarf and "extends GameCharacter" in dwarf
    assert "class_name Goblin" in goblin and "extends GameCharacter" in goblin


def test_board_script_turn_system():
    board = (FRONTEND / "scripts" / "board.gd").read_text(encoding="utf-8")
    assert "func try_move" in board
    assert "func _end_turn" in board            # пошаговость / переход хода
    assert "MOVES_PER_TURN" in board or "moves_left" in board
    assert "cards_left" in board                # лимит карт за ход
    assert "GameState.board_size" in board      # режим сообщает нода главного меню


def test_main_menu_buttons():
    menu = (FRONTEND / "scripts" / "main_menu.gd").read_text(encoding="utf-8")
    scene = (FRONTEND / "scenes" / "main_menu.tscn").read_text(encoding="utf-8")
    assert "room_list.tscn" in menu            # кнопка выбора комнат -> список комнат
    assert "start_local_mp" in menu            # локальный мультиплеер -> сцена поля
    for btn in ("RoomsButton", "CreateRoomButton", "LocalMpButton"):
        assert btn in scene, f"missing button {btn}"


def test_end_turn_button_present():
    scene = (FRONTEND / "scenes" / "board.tscn").read_text(encoding="utf-8")
    assert "EndTurnButton" in scene
    assert "Закончить ход" in scene


def test_cards_tags_and_art_exist():
    tags = (FRONTEND / "cards" / "tags.md").read_text(encoding="utf-8")
    assert "jump" in tags and "прыжок" in tags.lower()
    assert "игнорированием препятствий" in tags
    art = list((FRONTEND / "art").rglob("*.svg"))
    assert len(art) >= 4, "concept art + card art must be committed under frontend/art/"


def test_placeholder_contrast():
    """Placeholders must not blend into background: tiles are distinct colors."""
    board = (FRONTEND / "scripts" / "board.gd").read_text(encoding="utf-8")
    colors = re.findall(r'Color\(([\d.,\s]+)\)', board)
    assert len(colors) >= 9  # floor/wall/stone/gem/box/mush/crystal/Y/G + exits
    assert "draw_rect" in board and "draw_circle" in board
