# game_config.gd — единый файл конфигурации игры (issue #3).
# Размер поля, шаги за ход и использования карт за ход читаются только отсюда.
class_name GameConfig
extends RefCounted

const BOARD_SIZES: Array[int] = [6, 8, 10]   # квадратное поле произвольного размера
const DEFAULT_BOARD_SIZE: int = 8
const MOVES_PER_TURN: int = 3                # n клеток за ход (README: "3 хода")
const CARDS_PER_TURN: int = 1                # x карт за ход
const MAX_PLAYERS: int = 4                   # максимум игроков в комнате
const TOTAL_TURNS: int = 20                  # всего ходов до нашествия
const GOBLIN_START_TURN: int = 12            # с этого хода появляются гоблины
const STONES_TO_BUILD: int = 25              # камней для постройки крепости

# Сетевой сервер (backend на C++), пока не используется в локальном мультиплеере.
const SERVER_HOST: String = "127.0.0.1"
const SERVER_PORT: int = 9042

enum GameMode { LOCAL_MP, ONLINE }
