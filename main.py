#!/usr/bin/env python3
"""Точка входа игры «Гномья крепость».

Режимы:
  * интерактивная консоль (по умолчанию): `python main.py`;
  * авто-партия случайными ходами: `python main.py --auto [число_партий]`;
  * демонстрация правил (тестовый сценарий): `python main.py --demo`.
"""

from __future__ import annotations

import random
import sys

from game import Game
from game.constants import DIRECTIONS, GOBLINS_AFTER_TURN


def play_interactive(game: Game) -> None:
    print(__doc__)
    print("Управление: up/down/left/right — шаг; collect — собрать самоцвет;")
    print("exchange rocks|equip — обмен самоцвета; skip — пропустить ход;")
    print("quit — выйти.\n")
    while not game.game_over:
        print(game.render())
        name = f"Игрок {game.current_player + 1}"
        try:
            raw = input(f"{name}, действие ({game.active_player.actions_left}): "
                        ).strip().lower()
        except EOFError:
            break
        if raw in ("quit", "q", "exit"):
            break
        if raw == "skip":
            game.active_player.actions_left = 0
            game.end_turn()
            continue
        action: tuple | None = None
        if raw in DIRECTIONS:
            action = ("move", raw)
        elif raw == "collect":
            action = ("collect",)
        elif raw.startswith("exchange"):
            parts = raw.split()
            mode = parts[1] if len(parts) > 1 else "rocks"
            action = ("exchange", mode)
        if action is None:
            print("Не понял команду.\n")
            continue
        if not game.act(action):
            print("Действие не удалось (цель занята/нельзя толкнуть).\n")
    print(game.render())
    if game.fortress_built and game.winner:
        print(f"\nКрепость построена! Победитель — игрок {game.winner.idx + 1}.")
    elif game.game_over:
        print("\nКрепость не достроена — все проиграли.")


def auto_game(seed: int) -> dict:
    """Проигрывает партию случайными действиями и возвращает итог."""
    rng = random.Random(seed)
    game = Game(seed=seed)
    guard = 0
    while not game.game_over and guard < 10000:
        guard += 1
        choices = [("move", d) for d in DIRECTIONS]
        choices.append(("collect",))
        choices.append(("exchange", rng.choice(["rocks", "equip"])))
        game.act(rng.choice(choices))
        if rng.random() < 0.1:  # иногда игрок «пасуется»
            game.active_player.actions_left = 0
            game.end_turn()
    return {
        "seed": seed,
        "built": game.fortress_built,
        "mined": game.mined_rocks,
        "turn": game.turn,
        "winner": game.winner.idx + 1 if game.winner else None,
        "contributions": [p.contribution for p in game.players],
    }


def demo() -> None:
    """Короткая демонстрация ключевых механик на маленькой карте."""
    game = Game(level=["######",
                       "#.y..#",
                       "#....#",
                       "#.R..#",
                       "#.G..#",
                       "######"], seed=1)
    p = game.players[0]
    p.x, p.y = 2, 3  # игрок 1 под камнем (2,2), над зелёным лазером (2,4)
    print("Демонстрация: толкаем камень вверх — к краю поля.")
    print(game.render())
    game.move(p, "up")   # встанет на клетку камня? нет: толкнёт камень вверх
    game.move(p, "up")   # вытолкнет камень за край — камень добыт
    print(game.render())


if __name__ == "__main__":
    args = sys.argv[1:]
    if args and args[0] == "--auto":
        n = int(args[1]) if len(args) > 1 else 5
        for s in range(n):
            print(auto_game(seed=s))
    elif args and args[0] == "--demo":
        demo()
    else:
        play_interactive(Game(seed=random.randrange(10**6)))
