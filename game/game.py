"""Основная логика игры «Гномья крепость».

Реализация правил из README.md:
  * поле — sokoban-подобная сетка, толкание камней/коробок/лазеров;
  * камень добыт, если вытолкнут за пределы поля;
  * 4 игрока, по 3 действия за ход, всего 20 ходов;
  * лазеры светят во все 4 стороны (лучи проходят через пустые клетки и
    объекты, но не через стены);
  * зелёный лазер на игроке — можно проходить сквозь стены; если луч
    перестал светить, пока игрок в стене — игрок погибает и появляется
    в одном из 4 углов карты;
  * красный лазер на игроке — грибы и кристаллы ломаются толчком;
  * жёлтый лазер ломает камни (в т.ч. лучом) и убивает игроков на линии
    луча;
  * драгоценный камень обменивается на 3 обычных или 1 экипировку;
  * после 12-го хода на зелёных клетках появляются гоблины-лазутчики,
    убивающие игрока на ортогонально соседней с ними клетке.

Замечания по неоднозначным правилам (трактовки):
  * «После 12 хода» — гоблины появляются начиная с 13-го хода партии;
  * коробка не может быть выброшена за пределы поля (это не камень);
  * лазер нельзя столкнуть за пределы поля в другой объект; лазер,
    вытолкнутый за границу поля, разрушается (жёлтый — с зачётом камня);
  * гибель от гоблина/жёлтого лазера тоже приводит к возрождению в углу.
"""

from __future__ import annotations

import random
from dataclasses import dataclass

from .constants import (
    ACTIONS_PER_TURN,
    CELL_CHARS,
    DEFAULT_MAP,
    DEFAULT_SPAWNS,
    DIRECTIONS,
    GOBLINS_AFTER_TURN,
    ROCKS_TO_BUILD,
    TOTAL_TURNS,
    Tile,
)

CHAR_TILES = {v: k for k, v in CELL_CHARS.items()}

LASERS = (Tile.LASER_R, Tile.LASER_G, Tile.LASER_Y)
# Клетки, которые можно толкнуть.
PUSHABLE = {Tile.ROCK, Tile.BOX} | set(LASERS)


@dataclass
class Player:
    idx: int
    x: int
    y: int
    alive: bool = True
    in_wall: bool = False          # игрок «в стене» под зелёным лазером
    rocks: int = 0                 # добытые обычные камни (личный вклад)
    gems: int = 0                  # собранные драгоценные камни
    equipment: int = 0             # купленная экипировка
    actions_left: int = ACTIONS_PER_TURN

    @property
    def contribution(self) -> int:
        """Вклад игрока: добытые им камни + самоцветы + экипировка."""
        return self.rocks + self.gems + self.equipment


@dataclass
class Goblyn:
    x: int
    y: int


class Game:
    def __init__(self, level: list[str] | None = None, seed: int | None = None):
        rows = level if level is not None else DEFAULT_MAP
        self.grid = [[self._parse_char(c) for c in row] for row in rows]
        self.height = len(self.grid)
        self.width = len(self.grid[0]) if self.height else 0
        # Слой spawn-клеток: "y" — жёлтая (появляются камни),
        # "g" — зелёная (появляются гоблины), "" — обычная.
        self.spawn_layer = [
            ["y" if t == Tile.SPAWN_YELLOW else
             "g" if t == Tile.SPAWN_GREEN else "" for t in row]
            for row in self.grid
        ]
        self.spawn_yellow_cells = [
            (x, y) for y in range(self.height) for x in range(self.width)
            if self.spawn_layer[y][x] == "y"
        ]
        self.spawn_green_cells = [
            (x, y) for y in range(self.height) for x in range(self.width)
            if self.spawn_layer[y][x] == "g"
        ]
        self._rng = random.Random(seed)
        self.players = [
            Player(i, x, y) for i, (x, y) in enumerate(DEFAULT_SPAWNS)
        ]
        self.goblyns: list[Goblyn] = []
        self.turn = 0            # номер текущего хода партии (0..TOTAL_TURNS)
        self.current_player = 0  # индекс игрока, делающего ход
        self.mined_rocks = 0     # сколько камней всего сдано в стройку
        self.game_over = False
        self.fortress_built = False
        self.winner: Player | None = None
        self.log: list[str] = []

    # ------------------------------------------------------------------ utils
    @staticmethod
    def _parse_char(ch: str) -> Tile:
        return CHAR_TILES.get(ch, Tile.EMPTY)

    def in_bounds(self, x: int, y: int) -> bool:
        return 0 <= x < self.width and 0 <= y < self.height

    def tile_at(self, x: int, y: int) -> Tile | None:
        """None — клетка за пределами поля."""
        if not self.in_bounds(x, y):
            return None
        return self.grid[y][x]

    def player_at(self, x: int, y: int) -> Player | None:
        for p in self.players:
            if p.alive and p.x == x and p.y == y:
                return p
        return None

    def goblyn_at(self, x: int, y: int) -> Goblyn | None:
        for g in self.goblyns:
            if g.x == x and g.y == y:
                return g
        return None

    @property
    def active_player(self) -> Player:
        return self.players[self.current_player]

    # ---------------------------------------------------------------- lasers
    def laser_beams(self) -> dict[tuple[int, int], set[Tile]]:
        """Клетки, подсвеченные каждым типом лазера.

        Лазер светит лучами во все 4 стороны. Луч проходит через пустые
        клетки, spawn-клетки и объекты (камни, коробки, самоцветы, грибы,
        кристаллы), но останавливается на стенах и других лазерах.
        Подсвеченной считается и клетка препятствия (луч до неё доходит).
        Сам источник своим лучом не подсвечен.
        """
        beams: dict[tuple[int, int], set[Tile]] = {}
        for y in range(self.height):
            for x in range(self.width):
                t = self.grid[y][x]
                if t not in LASERS:
                    continue
                for dx, dy in DIRECTIONS.values():
                    nx, ny = x + dx, y + dy
                    while self.in_bounds(nx, ny):
                        nt = self.grid[ny][nx]
                        if nt == Tile.WALL or nt in LASERS:
                            if nt == Tile.WALL:
                                # стена подсвечена (для зелёного лазера —
                                # «проход сквозь стену»), дальше луч не идёт
                                beams.setdefault((nx, ny), set()).add(t)
                            break
                        beams.setdefault((nx, ny), set()).add(t)
                        nx, ny = nx + dx, ny + dy
        return beams

    def lasers_on_cell(self, x: int, y: int,
                      beams: dict | None = None) -> set[Tile]:
        if beams is None:
            beams = self.laser_beams()
        return beams.get((x, y), set())

    # ------------------------------------------------------------- mechanics
    def kill_player(self, player: Player, reason: str) -> None:
        """Игрок погибает и появляется в случайном из 4 свободных углов."""
        self.log.append(f"Игрок {player.idx + 1} погиб ({reason}).")
        corners = [(1, 1), (self.width - 2, 1),
                   (1, self.height - 2), (self.width - 2, self.height - 2)]
        free = [c for c in corners if self.player_at(*c) is None]
        pool = free if free else corners
        x, y = self._rng.choice(pool)
        player.x, player.y = x, y
        player.in_wall = False

    def _push(self, player: Player, tx: int, ty: int,
              dx: int, dy: int, beams: dict) -> bool:
        """Толкает объект из клетки (tx, ty) в направлении (dx, dy).

        Возвращает True, если толчок состоялся.
        """
        tile = self.grid[ty][tx]
        nx, ny = tx + dx, ty + dy

        # Грибы и кристаллы ломаются толчком, только если красный лазер
        # светит на игрока.
        if tile in (Tile.CRYSTAL, Tile.MUSHROOM):
            if Tile.LASER_R in self.lasers_on_cell(player.x, player.y, beams):
                self.grid[ty][tx] = Tile.EMPTY
                self.log.append(
                    f"Игрок {player.idx + 1} сломал "
                    f"{'кристалл' if tile == Tile.CRYSTAL else 'гриб'} толчком."
                )
                return True
            return False

        target = self.tile_at(nx, ny)

        # Выталкивание за пределы поля.
        if target is None:
            if tile == Tile.ROCK:
                # Камень добыт!
                self._clear_cell(ty, tx)
                self.mined_rocks += 1
                player.rocks += 1
                self.log.append(
                    f"Игрок {player.idx + 1} добыл камень "
                    f"(всего {self.mined_rocks}/{ROCKS_TO_BUILD})."
                )
                return True
            # Коробку и лазеры выбросить за пределы поля нельзя.
            return False

        # Целевая клетка должна быть свободна для объекта.
        if self.player_at(nx, ny) is not None or \
                self.goblyn_at(nx, ny) is not None:
            return False
        if target not in (Tile.EMPTY, Tile.SPAWN_YELLOW, Tile.SPAWN_GREEN):
            return False

        self.grid[nx][ny] = tile
        self._clear_cell(ty, tx)
        return True

    def _clear_cell(self, x: int, y: int) -> None:
        """Освобождает клетку: spawn-клетки остаются жёлтыми/зелёными."""
        spawn = self.spawn_layer[y][x]
        if spawn == "y":
            self.grid[y][x] = Tile.SPAWN_YELLOW
        elif spawn == "g":
            self.grid[y][x] = Tile.SPAWN_GREEN
        else:
            self.grid[y][x] = Tile.EMPTY

    def _apply_laser_effects(self, beams: dict) -> None:
        """Жёлтый лазер разрушает камни и убивает игроков на линии луча."""
        for (x, y), lit in sorted(beams.items()):
            if Tile.LASER_Y not in lit:
                continue
            t = self.grid[y][x]
            if t == Tile.ROCK:
                self.grid[y][x] = Tile.EMPTY
                self.log.append(f"Жёлтый лазер разрушил камень на ({x},{y}).")
            p = self.player_at(x, y)
            if p is not None and not p.in_wall:
                self.kill_player(p, "жёлтый лазер")

    # ------------------------------------------------------------------ move
    def move(self, player: Player, direction: str) -> bool:
        """Одно действие игрока: шаг в направлении. True — если удалось."""
        if self.game_over or not player.alive:
            return False
        if direction not in DIRECTIONS:
            raise ValueError(f"Неизвестное направление: {direction}")
        dx, dy = DIRECTIONS[direction]
        tx, ty = player.x + dx, player.y + dy
        beams = self.laser_beams()
        green_on_player = Tile.LASER_G in self.lasers_on_cell(
            player.x, player.y, beams)

        target = self.tile_at(tx, ty)

        # Зелёный лазер даёт проходить сквозь стены.
        if green_on_player and target == Tile.WALL:
            player.x, player.y = tx, ty
            player.in_wall = True
            return True

        if target is None:
            return False  # за пределы поля игрок не выходит

        if self.goblyn_at(tx, ty) is not None:
            return False  # на клетку гоблина войти нельзя

        if target in PUSHABLE or target in (Tile.CRYSTAL, Tile.MUSHROOM):
            if self._push(player, tx, ty, dx, dy, beams):
                new_t = self.grid[ty][tx]
                if new_t in (Tile.EMPTY, Tile.SPAWN_YELLOW, Tile.SPAWN_GREEN) \
                        and self.player_at(tx, ty) is None:
                    player.x, player.y = tx, ty
                    player.in_wall = False
                return True
            return False

        if target == Tile.WALL:
            return False

        # Пустая / spawn-клетка: обычный шаг.
        if self.player_at(tx, ty) is not None:
            return False
        player.x, player.y = tx, ty
        player.in_wall = False
        return True

    # ------------------------------------------------------------ collecting
    def collect(self, player: Player) -> bool:
        """Собрать драгоценный камень со своей клетки (действие)."""
        if self.game_over or not player.alive:
            return False
        if self.grid[player.y][player.x] == Tile.GEM:
            self.grid[player.y][player.x] = Tile.EMPTY
            player.gems += 1
            self.log.append(f"Игрок {player.idx + 1} нашёл самоцвет.")
            return True
        return False

    def exchange_gem(self, player: Player, mode: str) -> bool:
        """Обмен самоцвета: mode='rocks' -> 3 камня в стройку,
        mode='equip' -> 1 экипировка."""
        if self.game_over or player.gems < 1:
            return False
        if mode not in ("rocks", "equip"):
            return False
        player.gems -= 1
        if mode == "rocks":
            self.mined_rocks += 3
            player.rocks += 3
            self.log.append(
                f"Игрок {player.idx + 1} обменял самоцвет на 3 камня "
                f"(всего {self.mined_rocks}/{ROCKS_TO_BUILD})."
            )
        else:
            player.equipment += 1
            self.log.append(f"Игрок {player.idx + 1} купил экипировку.")
        return True

    # ------------------------------------------------------------ turn flow
    def act(self, action: tuple) -> bool:
        """Выполняет одно действие активным игроком.

        Формат действий:
          ("move", "up|down|left|right") | ("collect",) | ("exchange", mode)
        Возвращает True, если действие удалось. Если действий не осталось —
        автоматически завершается ход.
        """
        p = self.active_player
        if not self.game_over:
            # На свободные жёлтые клетки падают новые камни.
            self.replenish_rocks()
        kind = action[0]
        if kind == "move":
            ok = self.move(p, action[1])
        elif kind == "collect":
            ok = self.collect(p)
        elif kind == "exchange":
            ok = self.exchange_gem(p, action[1])
        else:
            raise ValueError(f"Неизвестное действие: {kind}")
        if ok:
            p.actions_left -= 1
            if p.actions_left <= 0:
                self.end_turn()
        return ok

    def end_turn(self) -> None:
        """Завершает ход активного игрока: эффекты, проверка победы, переход."""
        if self.game_over:
            return
        beams = self.laser_beams()
        self._apply_laser_effects(beams)

        # Кто остался в стене без зелёного луча — погибает.
        beams = self.laser_beams()
        for p in self.players:
            if p.in_wall:
                lit = self.lasers_on_cell(p.x, p.y, beams)
                if Tile.LASER_G not in lit:
                    self.kill_player(p, "остался в стене без зелёного лазера")

        # Гоблины убивают соседей (сверху/снизу/слева/справа).
        for g in self.goblyns:
            for p in self.players:
                if abs(p.x - g.x) + abs(p.y - g.y) == 1:
                    self.kill_player(p, "гоблин-лазутчик")

        if self.mined_rocks >= ROCKS_TO_BUILD:
            self.fortress_built = True
            self.game_over = True
            best = max(self.players, key=lambda q: q.contribution)
            self.winner = best
            self.log.append(
                f"Крепость построена! Победил игрок {best.idx + 1} "
                f"(вклад {best.contribution})."
            )
            return

        if self.turn >= TOTAL_TURNS:
            self.game_over = True
            self.log.append(
                "Нашествие началось, а крепость не достроена — все проиграли."
            )
            return

        # Ход переходит к следующему живому игроку.
        for _ in range(len(self.players)):
            self.current_player = (self.current_player + 1) % len(self.players)
            if self.players[self.current_player].alive:
                break
        self.active_player.actions_left = ACTIONS_PER_TURN

        # Полный круг четырёх игроков = новый ход партии.
        if self.current_player == 0:
            self.turn += 1
            if self.turn > GOBLINS_AFTER_TURN:
                self.spawn_goblyns()
                self.log.append(
                    f"Ход {self.turn}: в шахту проникли гоблины-лазутчики!")

    # --------------------------------------------------------------- spawning
    def spawn_goblyns(self) -> None:
        """Гоблины появляются на свободных зелёных клетках."""
        for y in range(self.height):
            for x in range(self.width):
                if self.grid[y][x] == Tile.SPAWN_GREEN and \
                        self.goblyn_at(x, y) is None and \
                        self.player_at(x, y) is None:
                    self.goblyns.append(Goblyn(x, y))

    def replenish_rocks(self) -> None:
        """На свободные жёлтые клетки падают новые камни.

        Spawn-клетка остаётся «жёлтой» (y), но визуально показывает камень;
        чтобы не терять информацию о клетке, храним слой spawn-клеток.
        """
        for (x, y) in self.spawn_yellow_cells:
            if self.grid[y][x] == Tile.SPAWN_YELLOW and \
                    self.player_at(x, y) is None and \
                    self.goblyn_at(x, y) is None:
                self.grid[y][x] = Tile.ROCK

    # ---------------------------------------------------------------- render
    def render(self) -> str:
        """Текстовое отображение. Клетка занимает 2 колонки:
        [символ клетки][маркеры лучей r/g/y]."""
        beams = self.laser_beams()
        header = "    " + "".join(f"{x % 10} " for x in range(self.width))
        lines = [header]
        for y in range(self.height):
            row = [f"{y % 10}  "]
            for x in range(self.width):
                p = self.player_at(x, y)
                g = self.goblyn_at(x, y)
                if p is not None:
                    cell = str(p.idx + 1)
                elif g is not None:
                    cell = "&"
                else:
                    cell = CELL_CHARS[self.grid[y][x]]
                lit = beams.get((x, y), set())
                marks = "".join(
                    ch for t, ch in ((Tile.LASER_R, "r"),
                                     (Tile.LASER_G, "g"),
                                     (Tile.LASER_Y, "y"))
                    if t in lit)
                row.append(cell + marks + " "[:1 if len(marks) == 3 else
                                               3 - len(marks)])
            lines.append("".join(row).rstrip())
        status = (
            f"Ход {self.turn}/{TOTAL_TURNS} | "
            f"Добыто камней: {self.mined_rocks}/{ROCKS_TO_BUILD} | "
            f"Ходит: игрок {self.current_player + 1} "
            f"(действий: {self.active_player.actions_left})"
        )
        players_info = " || ".join(
            f"И{p.idx + 1}: камней {p.rocks}, самоцветов {p.gems}, "
            f"экипировки {p.equipment}"
            for p in self.players
        )
        return "\n".join(lines) + "\n" + status + "\n" + players_info
