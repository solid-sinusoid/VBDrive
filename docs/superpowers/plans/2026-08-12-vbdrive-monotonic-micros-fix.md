# План исправления монотонной микросекундной метки VBDrive

> **Для агентного выполнения:** обязательно использовать `superpowers:executing-plans` и выполнять пункты последовательно. Для изменения кода обязательно соблюдать `superpowers:test-driven-development`.

**Цель:** исключить гонку TIM7 в `micros_64()`, заново прошить узлы 4 и 5 и подтвердить отсутствие 1-мс выбросов на 2000 синхронных циклах.

**Архитектура:** чистая функция объединяет согласованный снимок миллисекундной и микросекундной частей. STM32-обёртка снимает значения в короткой критической секции, а программный счётчик изменяется только TIM7 ISR. Протокол FOC-синхронизации и DSDL не меняются.

**Стек:** C++20, STM32 HAL/CMSIS, CMake/CTest, arm-none-eabi GCC, SocketCAN/Cyphal CAN FD.

## Общие ограничения

- Не менять частоту TIM4, DSDL и CAN subject IDs.
- Не включать силовую часть во время стендовой проверки.
- Сборку выполнять в `Release`; стенд использует один и тот же HEX для узлов 4 и 5.
- Все коммиты выполнять от `Ilya Uraev <ur.narmak@gmail.com>`.
- Документацию вести на русском языке.

---

### Задача 1: Регрессионный тест и атомарный снимок TIM7

**Файлы:**
- Создать: `App/monotonic_micros.hpp`
- Создать: `tests/test_monotonic_micros.cpp`
- Изменить: `tests/CMakeLists.txt`
- Изменить: `App/common.cpp:9,159-189`

**Интерфейсы:**
- Вход: `compose_micros_snapshot(uint64_t millis, uint32_t counter_before, bool update_pending, uint32_t counter_after)`.
- Выход: монотонная микросекундная метка `uint64_t`.

- [ ] **Шаг 1: добавить падающий host-тест**

Создать проверки обычного снимка, переполнения до первого чтения, переполнения между чтениями, отсутствия ложного переноса после второго чтения и перехода через `UINT32_MAX` миллисекунд:

```cpp
assert(compose_micros_snapshot(10, 120, false, 121) == 10121);
assert(compose_micros_snapshot(10, 3, true, 4) == 11004);
assert(compose_micros_snapshot(10, 999, false, 0) == 11000);
assert(compose_micros_snapshot(10, 998, false, 999) == 10999);
assert(compose_micros_snapshot(UINT32_MAX, 999, false, 0) == 4294967296000ULL);
```

- [ ] **Шаг 2: подтвердить RED**

Запустить:

```bash
cmake -S tests -B build/host-tests
cmake --build build/host-tests --parallel
ctest --test-dir build/host-tests --output-on-failure
```

Ожидаемый результат: сборка падает из-за отсутствующего `App/monotonic_micros.hpp` либо `compose_micros_snapshot`.

- [ ] **Шаг 3: реализовать минимальную чистую функцию**

`App/monotonic_micros.hpp` должен определять:

```cpp
constexpr std::uint64_t compose_micros_snapshot(
    const std::uint64_t millis,
    const std::uint32_t counter_before,
    const bool update_pending,
    const std::uint32_t counter_after)
{
    const bool wrapped = update_pending || (counter_after < counter_before);
    return (millis + (wrapped ? 1U : 0U)) * 1000U + counter_after;
}
```

- [ ] **Шаг 4: подключить атомарный снимок в прошивку**

В `App/common.cpp`:

- хранить `millis_k` как 64-битный `micros`;
- оставить приращение только в ветке TIM7 обработчика;
- в `micros_64()` сохранить `PRIMASK`, запретить прерывания, прочитать
  `millis_k`, TIM7, update flag и TIM7 повторно, восстановить `PRIMASK`, затем
  вызвать `compose_micros_snapshot`;
- сделать `millis_32()` простым приведением младших 32 бит `millis_k`.

- [ ] **Шаг 5: подтвердить GREEN и отсутствие регрессий**

Запустить:

```bash
cmake --build build/host-tests --parallel
ctest --test-dir build/host-tests --output-on-failure
```

Ожидаемый результат: оба теста проходят без предупреждений и ошибок.

- [ ] **Шаг 6: собрать Release-прошивку**

Запустить существующую Release-конфигурацию проекта и проверить успешный выход,
размер RAM/FLASH и наличие `build/Release/VBDrive.hex`.

- [ ] **Шаг 7: закоммитить исправление**

```bash
git add App/common.cpp App/monotonic_micros.hpp tests/CMakeLists.txt tests/test_monotonic_micros.cpp
git commit -m "fix(vbdrive): make micros snapshot monotonic"
```

### Задача 2: Прошивка и стендовая приёмка

**Файлы:**
- Использовать: `build/Release/VBDrive.hex`
- Использовать вне репозитория: `/tmp/vbdrive_sync_smoke.py`

**Интерфейсы:**
- Вход: CAN `can0`, узлы 4 и 5, SYNC master node ID 42.
- Выход: статусы `STAGED`, `APPLIED` и `apply_offset_microsecond` каждого узла.

- [ ] **Шаг 1: проверить артефакт и безопасное состояние**

Снять SHA-256 и размер HEX, убедиться, что `can0` находится в `ERROR-ACTIVE` с
`berr-counter tx=0 rx=0`, а `state.is_on=false` на узлах 4 и 5.

- [ ] **Шаг 2: прошить узлы 4 и 5 по CAN**

Использовать тот же CAN bootloader workflow, который ранее завершался
`flash_complete`, отдельно для node ID 4 и node ID 5. После каждого узла
проверить успешное завершение и отсутствие CAN errors.

- [ ] **Шаг 3: проверить версию и состояние после перезапуска**

Для обоих узлов прочитать `state.is_on`, `sync.version` и `sync.mode`.
Ожидаются `false`, `1`, `1` соответственно.

- [ ] **Шаг 4: выполнить две серии по 1000 циклов**

```bash
python3 /tmp/vbdrive_sync_smoke.py --interface can0 --cycles 1000 --start-cycle 1 --timeout-ms 30
python3 /tmp/vbdrive_sync_smoke.py --interface can0 --cycles 1000 --start-cycle 1001 --timeout-ms 30
```

Ожидается в каждой серии: `cycles_completed=1000`, нулевые значения
`missing_staged`, `missing_applied`, `duplicate_status_count`,
`rejected_or_timeout_count`, `negative_offset_count` и `result=PASS`.

- [ ] **Шаг 5: выполнить postflight**

Повторно подтвердить `state.is_on=false` на узлах 4 и 5 и CAN
`ERROR-ACTIVE (berr-counter tx 0 rx 0)`. Сохранить фактические median, p95, p99
и max межмоторного skew в итоговом отчёте пользователю.
