"""Тесты правил игры «Гномья крепость» (unittest, без внешних зависимостей).

Запуск:  python -m unittest discover tests -v
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from game import Game  # noqa: E402
from game.constants import (  # noqa: E402
    ACTIONS_PER_TURN,
    GOBLINS_AFTER_TURN,
    ROCKS_TO_BUILD,
    TOTAL_TURNS,
    Tile,
)


def small_level(rows):
    """Карта 6x6 с углами-спавнами; игроки будут расставлены вручную."""
    return [
        "######",
        "#....#",
        "#....#",
        "#....#",
        "#....#",
        "######",
    ] if rows is None else rows


class TestBasics(unittest.TestCase):
    def setUp(self):
        self.g = Game(level=small_level(None), seed=42)
        # Освободим углы и поставим игрока 1 в центр.
        for p in self.g.players[1:]:
            p.x, p.y = -5, -5  # «вне поля», чтобы не мешали
        self.p = self.g.players[0]
        self.p.x, self.p.y = 3, 3

    def test_default_map_is_rectangular(self):
        g = Game()
        self.assertTrue(all(len(row) == g.width for row in g.grid))
        self.assertEqual(g.height, 15)
        self.assertEqual(g.width, 15)

    def test_players_spawn_in_four_corners(self):
        g = Game()
        corners = {(1, 1), (g.width - 2, 1), (1, g.height - 2),
                   (g.width - 2, g.height - 2)}
        self.assertEqual({(p.x, p.y) for p in g.players}, corners)

    def test_three_actions_per_turn(self):
        self.assertEqual(ACTIONS_PER_TURN, 3)
        self.assertEqual(self.p.actions_left, 3)

    def test_wall_blocks_move(self):
        p = self.p
        p.x, p.y = 1, 3
        self.assertFalse(self.g.move(p, "left"))  # стена
        self.assertEqual((p.x, p.y), (1, 3))

    def test_out_of_bounds_move_rejected(self):
        p = self.p
        p.x, p.y = 1, 1
        # влево — стена, нельзя
        self.assertFalse(self.g.move(p, "left"))


class TestRocks(unittest.TestCase):
    def setUp(self):
        self.g = Game(level=[
            "#######",
            "#.....#",
            "#..o..#",   # камень в (3,2)
            "#..1..#" if False else "#.....#",
            "#.....#",
            "#######",
        ], seed=1)
        self.p = self.g.players[0]
        for q in self.g.players[1:]:
            q.x, q.y = -9, -9
        self.p.x, self.p.y = 4, 2  # справа от камня

    def test_push_rock(self):
        self.assertTrue(self.g.move(self.p, "left"))
        self.assertEqual(self.g.grid[2][2], Tile.ROCK)   # камень сдвинулся
        self.assertEqual((self.p.x, self.p.y), (3, 2))

    def test_rock_mined_when_pushed_off_field(self):
        self.p.x, self.p.y = 2, 2
        self.g.grid[2][1] = Tile.ROCK
        self.assertTrue(self.g.move(self.p, "left"))  # толкнул к краю
        self.assertEqual(self.g.mined_rocks, 1)
        self.assertEqual(self.p.rocks, 1)
        self.g.replenish_rocks()

    def test_cannot_push_two_rocks(self):
        self.g.grid[2][3] = Tile.ROCK
        self.g.grid[2][4] = Tile.ROCK
        self.p.x, self.p.y = 5, 2
        self.assertFalse(self.g.move(self.p, "left"))

    def test_yellow_cells_replenish(self):
        g = Game(level=["######",
                        "#y...#",
                        "#....#",
                        "#....#",
                        "######"], seed=1)
        g.replenish_rocks()
        self.assertEqual(g.grid[1][1], Tile.ROCK)


class TestBoxes(unittest.TestCase):
    def test_box_pushable_but_not_mined(self):
        g = Game(level=["#######",
                        "#.....#",
                        "#..B..#",
                        "#.....#",
                        "#######"], seed=1)
        p = g.players[0]
        for q in g.players[1:]:
            q.x, q.y = -9, -9
        p.x, p.y = 4, 2
        self.assertTrue(g.move(p, "left"))
        self.assertEqual(g.grid[2][2], Tile.BOX)
        # вытолкнуть коробку за поле нельзя
        p.x, p.y = 2, 2
        g.grid[2][1] = Tile.BOX
        self.assertFalse(g.move(p, "left"))
        self.assertEqual(g.mined_rocks, 0)


class TestLasers(unittest.TestCase):
    def make(self, rows):
        g = Game(level=rows, seed=7)
        for q in g.players[1:]:
            q.x, q.y = -9, -9
        return g

    def test_red_laser_breaks_mushroom_and_crystal(self):
        g = self.make(["#######",
                       "#.....#",
                       "#..m.C#",
                       "#..R..#",
                       "#.....#",
                       "#######"])
        p = g.players[0]
        p.x, p.y = 2, 2  # клетка слева подсвечена R снизу? проверим лучи
        beams = g.laser_beams()
        self.assertIn(Tile.LASER_R, beams.get((3, 2), set()))  # гриб
        self.assertIn(Tile.LASER_R, beams.get((5, 2), set()))  # кристалл
        # игрок должен стоять на подсвеченной клетке и толкать
        p.x, p.y = 4, 2  # между грибом и кристаллом, подсвечен R
        self.assertIn(Tile.LASER_R, beams.get((4, 2), set()))
        self.assertTrue(g.move(p, "right"))  # ломает кристалл
        self.assertEqual(g.grid[2][5], Tile.EMPTY)
        self.assertTrue(g.move(p, "left"))   # ломает гриб
        self.assertEqual(g.grid[2][3], Tile.EMPTY)

    def test_blocking_without_red_laser(self):
        g = self.make(["######",
                       "#....#",
                       "#.m..#",
                       "#....#",
                       "######"])
        p = g.players[0]
        p.x, p.y = 2, 2
        self.assertFalse(g.move(p, "up"))
        self.assertEqual(g.grid[2][2], Tile.MUSHROOM)

    def test_green_laser_wall_walk_and_death(self):
        g = self.make(["######",
                       "#....#",
                       "#G#..#",
                       "#....#",
                       "######"])
        p = g.players[0]
        p.x, p.y = 1, 2
        beams = g.laser_beams()
        self.assertIn(Tile.LASER_G, beams.get((2, 2), set()))  # стена светится
        self.assertTrue(g.move(p, "right"))  # шаг в стену
        self.assertTrue(p.in_wall)
        self.assertEqual(g.grid[2][2], Tile.WALL)
        # уберём лазер — при завершении хода игрок в стене погибнет
        g.grid[2][1] = Tile.EMPTY
        before = list(g.log)
        g.end_turn()
        self.assertTrue(any("остался в стене" in s for s in g.log))
        self.assertNotEqual((p.x, p.y), (2, 2))

    def test_yellow_laser_kills_player_on_beam(self):
        g = self.make(["######",
                       "#....#",
                       "#.Y..#",
                       "#....#",
                       "######"])
        p = g.players[0]
        p.x, p.y = 4, 2
        beams = g.laser_beams()
        self.assertIn(Tile.LASER_Y, beams.get((4, 2), set()))
        g._apply_laser_effects(beams)
        self.assertTrue(any("жёлтый лазер" in s for s in g.log))
        # возродился в углу
        self.assertIn((p.x, p.y), [(1, 1), (4, 1), (1, 3), (4, 3)])

    def test_yellow_laser_destroys_rock(self):
        g = self.make(["######",
                       "#....#",
                       "#.Y.o#",
                       "#....#",
                       "######"])
        beams = g.laser_beams()
        g._apply_laser_effects(beams)
        self.assertEqual(g.grid[2][4], Tile.EMPTY)

    def test_laser_can_be_pushed(self):
        g = self.make(["#######",
                       "#.....#",
                       "#..R..#",
                       "#.....#",
                       "#######"])
        p = g.players[0]
        p.x, p.y = 4, 2
        self.assertTrue(g.move(p, "left"))
        self.assertEqual(g.grid[2][2], Tile.LASER_R)
        self.assertEqual((p.x, p.y), (3, 2))


class TestGemsEconomy(unittest.TestCase):
    def make(self):
        g = Game(level=["######",
                        "#.*..#",
                        "#....#",
                        "#....#",
                        "######"], seed=3)
        for q in g.players[1:]:
            q.x, q.y = -9, -9
        p = g.players[0]
        p.x, p.y = 2, 1
        return g, p

    def test_collect_gem(self):
        g, p = self.make()
        self.assertTrue(g.collect(p))
        self.assertEqual(p.gems, 1)
        self.assertEqual(g.grid[1][2], Tile.EMPTY)
        self.assertFalse(g.collect(p))  # собирать больше нечего

    def test_gem_to_three_rocks(self):
        g, p = self.make()
        g.collect(p)
        self.assertTrue(g.exchange_gem(p, "rocks"))
        self.assertEqual(g.mined_rocks, 3)
        self.assertEqual(p.rocks, 3)
        self.assertEqual(p.gems, 0)

    def test_gem_to_equipment(self):
        g, p = self.make()
        g.collect(p)
        self.assertTrue(g.exchange_gem(p, "equip"))
        self.assertEqual(p.equipment, 1)
        self.assertEqual(g.mined_rocks, 0)

    def test_exchange_without_gem_fails(self):
        g, p = self.make()
        self.assertFalse(g.exchange_gem(p, "rocks"))


class TestTurnFlow(unittest.TestCase):
    def test_win_at_25_rocks_and_winner_by_contribution(self):
        g = Game(level=small_level(None), seed=5)
        p1 = g.players[0]
        p2 = g.players[1]
        p1.x, p1.y = 1, 1
        p2.x, p2.y = 4, 1
        for q in g.players[2:]:
            q.x, q.y = -9, -9
        g.mined_rocks = 20
        p1.rocks = 20
        p2.rocks = 0
        # добираем ещё 5 камней «обменом»
        p2.gems = 0
        g.current_player = 0
        g.active_player.actions_left = 1
        # симулируем добычу: толкнём камни руками
        g.grid[1][2] = Tile.ROCK
        g.grid[1][3] = Tile.ROCK
        g.act(("move", "right"))  # p1 толкает камень (2,1)->(3,1)? нет: встанет
        # проще — напрямую добавим счётчик и завершим ход
        g.mined_rocks = ROCKS_TO_BUILD
        p1.rocks = ROCKS_TO_BUILD
        g.act(("move", "down"))
        self.assertTrue(g.fortress_built)
        self.assertTrue(g.game_over)
        self.assertIs(g.winner, p1)

    def test_lose_after_20_turns(self):
        g = Game(level=small_level(None), seed=6)
        g.turn = TOTAL_TURNS
        g.current_player = 3
        g.players[3].actions_left = 1
        g.act(("move", "up"))  # последнее действие 4-го игрока -> конец круга
        self.assertTrue(g.game_over)
        self.assertFalse(g.fortress_built)
        self.assertIsNone(g.winner)

    def test_turn_counter_advances_full_circle(self):
        g = Game(level=small_level(None), seed=8)
        for _ in range(4):
            g.active_player.actions_left = 1
            # неудачное действие не тратит действий — используем skip-механику
            g.active_player.actions_left = 0
            g.end_turn()
        self.assertEqual(g.turn, 1)


class TestGoblyns(unittest.TestCase):
    def test_goblyns_spawn_after_turn_12(self):
        g = Game(level=["######",
                        "#g...#",
                        "#....#",
                        "#...g#",
                        "######"], seed=9)
        g.turn = GOBLINS_AFTER_TURN  # 12 — гоблинов ещё нет
        self.assertEqual(g.goblyns, [])
        g.spawn_goblyns()
        self.assertEqual(len(g.goblyns), 2)
        self.assertEqual({(gb.x, gb.y) for gb in g.goblyns}, {(1, 1), (4, 3)})

    def test_goblyn_kills_adjacent_player(self):
        g = Game(level=["######",
                        "#g...#",
                        "#....#",
                        "######"], seed=10)
        p = g.players[0]
        for q in g.players[1:]:
            q.x, q.y = -9, -9
        p.x, p.y = 2, 1
        from game.game import Goblyn
        g.goblyns.append(Goblyn(1, 1))
        g.end_turn()
        self.assertTrue(any("гоблин" in s for s in g.log))

    def test_cannot_step_onto_goblyn(self):
        g = Game(level=["######",
                        "#....#",
                        "#....#",
                        "######"], seed=11)
        p = g.players[0]
        for q in g.players[1:]:
            q.x, q.y = -9, -9
        p.x, p.y = 2, 2
        from game.game import Goblyn
        g.goblyns.append(Goblyn(3, 2))
        self.assertFalse(g.move(p, "right"))


class TestDefaultLevelSanity(unittest.TestCase):
    def test_default_level_has_required_cells(self):
        g = Game()
        flat = [t for row in g.grid for t in row]
        self.assertGreaterEqual(flat.count(Tile.SPAWN_YELLOW), 8)
        self.assertGreaterEqual(flat.count(Tile.SPAWN_GREEN), 3)
        self.assertIn(Tile.LASER_R, flat)
        self.assertIn(Tile.LASER_G, flat)
        self.assertIn(Tile.ROCK, flat)
        self.assertIn(Tile.GEM, flat)
        self.assertIn(Tile.MUSHROOM, flat)
        self.assertIn(Tile.CRYSTAL, flat)
        self.assertIn(Tile.BOX, flat)


if __name__ == "__main__":
    unittest.main()
