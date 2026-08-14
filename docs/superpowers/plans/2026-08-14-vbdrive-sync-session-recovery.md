# План восстановления сессии синхронизации VBDrive

> **Для агентных исполнителей:** ОБЯЗАТЕЛЬНЫЙ ДОПОЛНИТЕЛЬНЫЙ НАВЫК: использовать `superpowers:subagent-driven-development` (рекомендуется) или `superpowers:executing-plans` для пошагового выполнения этого плана. Для отслеживания шагов используются флажки (`- [ ]`).

**Цель:** устранить `REJECTED/TOO_FAR_AHEAD` при переходе от последовательного activation hold к общему runtime-циклу и обеспечить безопасный перезапуск нумерации после выключения двигателя.

**Архитектура:** `FocCycleSync` принимает любой модульно более новый номер, когда свободный двухслотовый буфер способен принять команду; ограничение числа одновременно подготовленных команд остаётся неизменным. Новый `reset_session()` очищает только состояние текущей командной сессии и вызывается после фактического выключения двигателя либо watchdog shutdown, не меняя сохранённый `sync.mode`.

**Технологии:** C++20, `std::atomic`, CMake/CTest host-тесты, STM32G431 firmware, Cyphal/CAN FD, существующий `tools/flash_vbdrive_release.py`.

## Общие ограничения

- Не добавлять новые Cyphal subscriptions и не изменять DSDL.
- Не использовать динамическую память в runtime и ISR-пути.
- Два теневых слота остаются жёстким пределом; третья команда возвращает `NO_FREE_SLOT`.
- `reset_session()` вызывается только после выключения силовой части и не меняет `sync.mode`.
- Прошивка выполняется только из чистой проверенной release-ветки существующим fail-closed wrapper.
- Моторы не активируются во время проверки прошивки.
- Все коммиты создаются от имени `Ilya Uraev <ur.narmak@gmail.com>`.

---

### Задача 1: Зафиксировать новое поведение автомата тестами

**Файлы:**
- Изменить: `tests/test_foc_cycle_sync.cpp`

**Интерфейсы:**
- Использует: `FocCycleSync::stage()`, `on_sync()`, `consume_armed()`, `complete_apply()`.
- Фиксирует: переход через пропущенные номера, старый номер без reset и начало новой сессии после `reset_session()`.

- [ ] **Шаг 1: написать failing-тест перехода `2 -> 20`**

Добавить сценарий, который применяет цикл `2`, освобождает слот и ожидает успешный `STAGED` для цикла `20`:

```cpp
void test_idle_session_accepts_forward_cycle_gap()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(2), true, 100) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(2, 110) == SyncResult::Armed);
    const auto applied = sync.consume_armed(111);
    assert(applied.has_value());
    sync.complete_apply(*applied, true);
    (void) require_status(sync);

    assert(sync.stage(command(20), true, 200) == StageResult::Staged);
    const auto staged = require_status(sync);
    assert(staged.status == StatusCode::Staged);
    assert(staged.reason == StatusReason::None);
}
```

- [ ] **Шаг 2: написать failing-тест новой сессии**

Добавить сценарий `20 -> reset_session() -> 0` и контроль, что без reset старый номер остаётся `STALE`:

```cpp
void test_reset_session_accepts_restarted_cycle_counter()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(20), true, 100) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(20, 110) == SyncResult::Armed);
    const auto applied = sync.consume_armed(111);
    assert(applied.has_value());
    sync.complete_apply(*applied, true);
    (void) require_status(sync);

    assert(sync.stage(command(19), true, 120) == StageResult::Rejected);
    assert(require_status(sync).reason == StatusReason::Stale);

    sync.reset_session();
    assert(sync.stage(command(0), true, 200) == StageResult::Staged);
    assert(require_status(sync).status == StatusCode::Staged);
}
```

- [ ] **Шаг 3: зарегистрировать оба сценария в `main()`**

```cpp
test_idle_session_accepts_forward_cycle_gap();
test_reset_session_accepts_restarted_cycle_counter();
```

- [ ] **Шаг 4: запустить RED**

```bash
cmake -S tests -B build/host-tests
cmake --build build/host-tests -j2
ctest --test-dir build/host-tests --output-on-failure
```

Ожидается ошибка компиляции из-за отсутствующего `reset_session()`. После временного объявления метода без изменения `stage()` тест перехода `2 -> 20` должен падать на ожидании `StageResult::Staged`, фактически получая `Rejected/TooFarAhead`.

### Задача 2: Реализовать границы sync-сессии

**Файлы:**
- Изменить: `App/foc_cycle_sync.hpp`
- Изменить: `App/foc_cycle_sync.cpp`
- Проверить: `tests/test_foc_cycle_sync.cpp`

**Интерфейсы:**
- Создаёт: `void FocCycleSync::reset_session()`.
- Меняет: `FocCycleSync::stage()` больше не считает пропуск свободных cycle-ID ошибкой `TooFarAhead`.
- Сохраняет: `STALE`, `NO_FREE_SLOT`, модульное сравнение и двухслотовый предел.

- [ ] **Шаг 1: объявить `reset_session()`**

В public-секции `FocCycleSync` добавить:

```cpp
void reset_session();
```

- [ ] **Шаг 2: реализовать единый сброс сессии**

Перенести очистку автомата из `set_mode()` в `reset_session()`, не присваивая `mode_`:

```cpp
void FocCycleSync::reset_session()
{
    armed_slot_.store(no_slot, std::memory_order_release);
    for (auto& slot : slots_) {
        slot.state.store(SlotState::Empty, std::memory_order_release);
    }
    applied_mailbox_.valid.store(false, std::memory_order_release);
    immediate_apply_.valid.store(false, std::memory_order_release);
    has_last_applied_.store(false, std::memory_order_release);
    has_last_sync_ = false;
    watchdog_reported_ = false;
    main_status_head_ = 0;
    main_status_tail_ = 0;
}

void FocCycleSync::set_mode(const SyncMode mode)
{
    reset_session();
    mode_ = mode;
}
```

- [ ] **Шаг 3: удалить числовое ограничение на свободную сессию**

В `stage()` сохранить проверку `cycle_after()`, но удалить ветку:

```cpp
if (static_cast<std::uint16_t>(command.cycle_id - last) > slots_.size()) {
    // REJECTED/TOO_FAR_AHEAD
}
```

Фактическая глубина конвейера остаётся ограничена поиском свободного элемента `slots_`.

- [ ] **Шаг 4: запустить GREEN и sanitizer**

```bash
cmake -S tests -B build/host-tests \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build/host-tests -j2
ctest --test-dir build/host-tests --output-on-failure
```

Ожидается `2/2` теста без sanitizer diagnostics.

### Задача 3: Подключить reset к безопасному выключению

**Файлы:**
- Изменить: `App/app.cpp`
- Проверить: `tests/test_foc_cycle_sync.cpp`

**Интерфейсы:**
- Использует: `FocCycleSync::reset_session()`.
- Точки вызова: `stop_motor_if_requested()` после `motor->set_state(false)` и watchdog shutdown после выключения силовой части.

- [ ] **Шаг 1: сбрасывать сессию после `state.is_on=false`**

```cpp
static void stop_motor_if_requested() {
    if (!motor_stop_pending) {
        return;
    }
    motor_stop_pending = false;
    motor->set_state(false);
    foc_cycle_sync.reset_session();
}
```

- [ ] **Шаг 2: сбрасывать сессию после watchdog shutdown**

```cpp
if (foc_cycle_sync.poll_watchdog(micros_64()) == WatchdogAction::Disable) {
    motor->set_foc_point(FOCTarget{0});
    motor->set_current_regulator_params(0.0f, 0.0f);
    motor->set_state(false);
    foc_cycle_sync.reset_session();
}
```

- [ ] **Шаг 3: проверить host-тесты без sanitizer-кэша**

```bash
cmake -S tests -B build/host-tests-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-tests-release -j2
ctest --test-dir build/host-tests-release --output-on-failure
```

Ожидается `2/2` теста.

- [ ] **Шаг 4: закоммитить firmware-исправление**

```bash
git add App/app.cpp App/foc_cycle_sync.cpp App/foc_cycle_sync.hpp tests/test_foc_cycle_sync.cpp
git -c user.name='Ilya Uraev' -c user.email='ur.narmak@gmail.com' \
  commit -m 'fix(sync): recover cycle session after motor shutdown'
```

### Задача 4: Собрать и интегрировать release firmware

**Файлы:**
- Использовать: `build/Release/VBDrive.hex`
- Использовать: `tools/flash_vbdrive_release.py`
- Изменить только при необходимости release-политики: `tools/flash_vbdrive_release.py`

**Интерфейсы:**
- Вход: чистая ветка `fix/vbdrive-sync-session-recovery` на базе `safety/temperature-telemetry-20260729`.
- Выход: проверенный `VBDrive.hex`, затем fast-forward/merge в `safety/temperature-telemetry-20260729` для допуска fail-closed wrapper.

- [ ] **Шаг 1: собрать ARM Release**

```bash
cmake --preset Release
cmake --build --preset Release -j2
arm-none-eabi-size build/Release/VBDrive.elf
sha256sum build/Release/VBDrive.hex
```

Ожидается успешная сборка без новых предупреждений и переполнения FLASH/RAM.

- [ ] **Шаг 2: проверить историю и чистоту**

```bash
git status --short
git log -2 --format='%h %an <%ae> %s'
git merge-base --is-ancestor safety/temperature-telemetry-20260729 HEAD
```

Ожидается пустой status и успешный ancestor-check.

- [ ] **Шаг 3: интегрировать fix в release-ветку**

Переключить этот worktree обратно на `safety/temperature-telemetry-20260729` и выполнить fast-forward merge:

```bash
git switch safety/temperature-telemetry-20260729
git merge --ff-only fix/vbdrive-sync-session-recovery
```

- [ ] **Шаг 4: выполнить dry-run release wrapper**

```bash
python3 tools/flash_vbdrive_release.py --joint 1
```

Ожидается проверенный образ, release audit и успешный dry-run CAN flasher без открытия CAN для записи.

### Задача 5: Прошить шесть приводов и выполнить безопасный postflight

**Файлы:**
- Использовать: `build/Release/VBDrive.hex`
- Использовать: `build/Release/flash_release_audit.json`

**Интерфейсы:**
- Вход: `can0`, отключённые узлы `1..6`, release wrapper.
- Выход: одинаковая firmware на шести узлах, `state.is_on=false`, `sync.version=1`, `sync.mode=1`.

- [ ] **Шаг 1: проверить CAN и выключенное состояние**

```bash
ip -details -statistics link show can0
for n in 1 2 3 4 5 6; do
  timeout 5s yakut register-access "$n" state.is_on
done
```

Ожидается `ERROR-ACTIVE`, `berr-counter tx 0 rx 0` и `false` на каждом узле.

- [ ] **Шаг 2: прошить узлы последовательно**

Для каждого `n` от 1 до 6 выполнить:

```bash
python3 tools/flash_vbdrive_release.py --joint "$n" --execute
```

После каждого переноса проверить возвращение application node и `state.is_on=false`. Не переходить к следующему узлу при ошибке transfer или readback.

- [ ] **Шаг 3: проверить версии и CAN postflight**

```bash
for n in 1 2 3 4 5 6; do
  timeout 5s yakut register-access "$n" state.is_on
  timeout 5s yakut register-access "$n" sync.version
  timeout 5s yakut register-access "$n" sync.mode
done
ip -details -statistics link show can0
```

Ожидается `false`, `1`, `1` для каждого узла и неизменившиеся CAN error counters.

- [ ] **Шаг 4: передать оператору повторный MoveIt-запуск**

Длительный runtime самостоятельно не запускать. Оператор повторяет прежнюю команду `ros2 launch minipulator_moveit_config moveit.launch.py hardware:=real ...`. Успех: нет `reason=4`, hardware остаётся `active`, оба контроллера переходят в `active`.

### Задача 6: Финальная проверка и завершение ветки

**Файлы:**
- Проверить: Git history, host test logs, release audit.

**Интерфейсы:**
- Выход: чистая release-ветка с тестируемым исправлением и воспроизводимым firmware SHA-256.

- [ ] **Шаг 1: повторить свежие тесты**

```bash
cmake --build build/host-tests-release -j2
ctest --test-dir build/host-tests-release --output-on-failure
```

- [ ] **Шаг 2: проверить репозиторий и audit**

```bash
git status --short
git log -2 --format='%h %an <%ae> %s'
python3 -m json.tool build/Release/flash_release_audit.json
```

- [ ] **Шаг 3: удалить только уже слитую fix-ветку**

```bash
git branch -d fix/vbdrive-sync-session-recovery
```

Не удалять canonical firmware worktree `/home/vladimir/rbs_ws/.worktrees/vbdrive-temperature-safety`.
