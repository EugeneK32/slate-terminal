# Slate Terminal

**A compact GTK4 terminal for GNOME — native window, integrated tabs, and shortcuts that don't depend on the Russian/English keyboard layout.**

**Slate Terminal** — небольшой терминал для Linux на **GTK4 + libadwaita + VTE**, созданный с упором на удобство в GNOME: одна шапка с вкладками, аккуратное тёмное оформление Adwaita и горячие клавиши по физическим клавишам (RU/EN).

> **Статус: v0.2.2, экспериментальный проект.** Приложение уже запускалось на Arch Linux / GNOME Wayland, однако пока не проходило полноценное тестирование на разных системах. Это исходный код, не готовый бинарный пакет.

## Возможности

- Нативное окно GNOME с **одной** шапкой, вкладками и кнопками управления.
- Вкладки, поиск по выводу, копирование и вставка, масштабирование текста.
- Физические сочетания клавиш для RU/EN на стандартной Linux-клавиатуре (evdev/XKB).
- Настройки шрифта, размера, интервала между строками и отступов **прямо в приложении**, без перезапуска.
- Цветовая схема Adwaita Dark; стабильный Cairo-рендерер по умолчанию.
- Работа с Bash, Zsh, SSH и терминальными программами через VTE.
- Иконка и `.desktop`-файл для меню GNOME.
- Запуск в указанном каталоге: `slate-terminal --working-directory /path/to/folder`.

## Установка (Arch Linux)

Нужны Arch Linux, GTK4, libadwaita и VTE4. Установщик сам поставит зависимости через `pacman` и соберёт приложение **для текущего пользователя**:

```bash
# После скачивания и распаковки архива или клонирования репозитория:
cd slate-terminal
./install.sh
```

Запуск:

```bash
slate-terminal
```

Устанавливаемые пути:

| Назначение | Путь |
| --- | --- |
| Программа | `~/.local/bin/slate-terminal` |
| Настройки | `~/.config/slate-terminal/config.ini` |
| Ярлык GNOME | `~/.local/share/applications/dev.slate.Terminal.desktop` |
| Иконка | `~/.local/share/icons/hicolor/512x512/apps/dev.slate.Terminal.png` |
| Привязки для Zsh | `~/.config/slate-terminal/slate-keybindings.zsh` |

`install.sh` **не удаляет другие терминалы** и не перезаписывает существующий `config.ini`. При изменении `.zshrc` создаёт резервную копию. Запускайте установщик **без `sudo`** — он сам запросит права при установке зависимостей.

Если команда `slate-terminal` не находится, убедитесь, что `~/.local/bin` присутствует в `PATH`, или запустите `~/.local/bin/slate-terminal`.

## Горячие клавиши

| Сочетание | Действие |
| --- | --- |
| `Ctrl+Shift+T` / `Ctrl+Shift+W` | Новая / закрыть вкладку |
| `Ctrl+Shift+N` | Новое окно |
| `Ctrl+Shift+C` / `Ctrl+Shift+V` | Копировать / вставить |
| `Ctrl+Shift+F` | Поиск по тексту |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Следующая / предыдущая вкладка |
| `Ctrl+PageUp` / `Ctrl+PageDown` | Предыдущая / следующая вкладка |
| `Alt+1…9` | Выбрать вкладку по номеру |
| `Ctrl++` / `Ctrl+-` / `Ctrl+0` | Увеличить / уменьшить / сбросить масштаб |
| `Ctrl+,` | Настройки внешнего вида |

На стандартных Linux-клавиатурах буквенные сочетания определяются по *позиции клавиши*, а не по символу текущей раскладки. Это не глобальный фикс GTK4 для всех приложений и не универсальное решение для произвольных клавиатурных схем.

**Home / End / Delete / Ctrl+стрелки:** Slate отправляет совместимые escape-последовательности. Для Zsh установщик подключает `shell/slate-keybindings.zsh`; для других оболочек и удалённых сеансов итоговое поведение зависит от их собственных настроек.

**Выделение через Shift+стрелки:** его выполняет Zsh с отдельным плагином (например, `zsh-shift-select`), а не сам VTE. Для такого выделения предусмотрен мост в `wl-copy`; он работает только в локальном Zsh на переднем плане. Обычное выделение **мышью** и `Ctrl+Shift+C` работают напрямую через VTE. Плагин `zsh-shift-select` не устанавливается автоматически.

## Настройки

Нажмите кнопку настройки в правой части шапки или `Ctrl+,`. Настройки применяются к вкладкам текущего окна и записываются в:

```text
~/.config/slate-terminal/config.ini
```

Пример файла:

```ini
[Appearance]
font=Adwaita Mono 12
background=#1e1e1e
foreground=#e7e7e7
line_spacing=1.16
padding=12

[Graphics]
renderer=cairo

[Terminal]
shell=
```

`renderer=cairo` установлен по умолчанию, поскольку на одной из проверенных конфигураций GNOME/Wayland GPU-рендеринг вызывал артефакты шрифта. Для теста ускоренной отрисовки можно запустить `GSK_RENDERER=gl slate-terminal`. После смены рендерера требуется полностью перезапустить программу.

Параметр `shell=` можно оставить пустым, чтобы использовать оболочку пользователя из системного профиля, либо указать абсолютный путь, например `/usr/bin/zsh`.

## «Открыть в терминале» в GNOME Files

Slate умеет принимать рабочий каталог:

```bash
slate-terminal --working-directory "$HOME/Downloads"
```

Для пункта контекстного меню в Nautilus можно использовать внешний проект [`nautilus-open-any-terminal`](https://github.com/Stunkymonkey/nautilus-open-any-terminal) (в Arch — AUR). Пример настройки для **локальных каталогов**:

```bash
gsettings set com.github.stunkymonkey.nautilus-open-any-terminal terminal custom
gsettings set com.github.stunkymonkey.nautilus-open-any-terminal custom-local-command 'slate-terminal --working-directory %s'
nautilus -q
```

Расширение не входит в состав Slate. Единого настройки «терминал по умолчанию» для всех программ GNOME нет; отдельные приложения могут использовать собственный способ запуска.

## Разработка

Сборка вручную при наличии зависимостей (`gtk4`, `libadwaita-1`, `vte-2.91-gtk4`, `pkg-config`, C compiler):

```bash
make
./build/slate-terminal
```

Проверки:

```bash
./tests/check.sh
```

Очистка и удаление пользовательского бинарника, иконки и ярлыка:

```bash
make clean
make uninstall
```

`make uninstall` **не удаляет пользовательские настройки и изменения `.zshrc`** — их при необходимости следует убрать отдельно.

Сборка автоматически проверяется в GitHub Actions на Arch Linux. Дополнительные сведения см. в [CONTRIBUTING.md](CONTRIBUTING.md) и [CHANGELOG.md](CHANGELOG.md).

## Известные ограничения

- Разрабатывается прежде всего для Arch + GNOME/Wayland; другие среды пока проверены хуже.
- Привязки по физическим клавишам рассчитаны на обычную evdev/XKB-клавиатуру.
- Возможны нюансы с управлением терминальными приложениями, выделением в оболочке и разными рендерерами GTK4.
- В приложении пока нет полной настройки всех сочетаний клавиш через GUI.

## Лицензия

[MIT](LICENSE) — Copyright © 2026 Slate Terminal contributors.
