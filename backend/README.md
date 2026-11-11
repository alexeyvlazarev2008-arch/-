# Backend — «Гномья крепость» (C++)

Игровой сервер для сетевой версии настолки. Реализует:
- **комнаты (лобби)**: создание, список, подключение; максимум **4 игрока** в комнате;
- **игровую логику**: поле 6×6 / 8×8 / 10×10, толкание камней/коробок, добыча камня
  выталкиванием за край поля, лазеры (красный/зелёный/жёлтый), гоблины с 12-го хода,
  победа при 25 добытых камнях, поражение после 20 ходов;
- **пошаговость**: `moves_per_turn` шагов и лимит карт за ход (конфиг в `include/game_state.hpp`,
  синхронизирован с `frontend/game_config.gd`).

## Протокол
TCP, newline-delimited JSON:
```
-> {"type":"create_room","name":"alpha"}   <- {"type":"room_created","id":"..."}
-> {"type":"list_rooms"}                   <- {"type":"rooms","ids":[...]}
-> {"type":"join","room":"id","player":"Thorin"}
-> {"type":"start"} | move{dx,dy} | card{card} | end_turn | state
```

## Сборка и тесты
```
cmake -S backend -B backend/build && cmake --build backend/build -j
./backend/build/test_game      # unit-тесты
./backend/build/server 9042    # запуск сервера
```
Зависимости только стандартные: C++17, pthread (POSIX sockets). Godot-клиент подключается
через `StreamPeerTCP` (см. `frontend/net/network_client.gd`).
