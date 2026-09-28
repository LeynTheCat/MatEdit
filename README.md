# MatEdit

## ENG

**MatEdit** is a modern desktop editor for visualizing, creating, and editing game material definitions and physical material data used by Xash3D / Xash3D FWGS projects. It supports `.mat` and `.def` workflows, material preview, texture assignment, and real-time preview of lighting and PBR material behavior.

The project has evolved from a basic material editor into a cross-platform tool with CI packaging, contributor-aware metadata, and nightly builds for Windows and Linux.

---

## Features

- **PBR-style material preview** with real-time lighting and material parameter visualization.
- **Texture map support**:
  - Albedo / diffuse
  - Normal maps
  - Gloss / specular / AO
  - Luma / emissive maps
  - Height / bump / parallax-related data
- **Skybox environment loading** using DDS cubemaps for realistic reflections and background.
- **Material and physics editing**:
  - Parsing and saving `.mat` files
  - Editing physical material definitions (`materials.def` / similar game data)
  - Sound properties, impact settings, and decals
- **Interactive 3D viewport**:
  - Cube, sphere, plane, cylinder, cone, torus, and teapot primitives
  - Orbit camera controls and adjustable lighting modes
- **Embedded shader pipeline** without requiring external shader files next to the executable.
- **Cross-platform build setup** with CMake and automated nightly packaging for Windows and Linux.
- **Contributor support** for displaying contributor data in the app and packaging assets.

---

## Project structure

- `src/main.cpp` — Application entry point, render loop, camera logic, and frame limiter.
- `src/MaterialSystem.cpp / .h` — Material parsing, texture/material loading, physical property handling.
- `src/EditorUI.cpp / .h` — UI panels, file browser, previews, and editing tools built with Dear ImGui.
- `src/Shader.cpp / .h` and `src/ShadersSource.h` — In-memory shader compilation and OpenGL program management.
- `src/Config.cpp / .h` — Editor configuration.
- `src/ImageLoader.cpp` — Texture/image loading logic.
- `src/Skybox.cpp` — DDS cubemap skybox handling.
- `src/PostProcess.cpp` — Post-processing pipeline.
- `src/Contributors.cpp` — Contributor-aware runtime data integration.
- `src/shaders/` — Reference shader sources.
- `external/` — Third-party dependencies such as GLFW, GLAD, GLM, GLI, stb, and Dear ImGui.
- `cmake/` — CMake helpers.
- `CONTRIBUTORS.txt` — List of project contributors used by the app.

---

## Build instructions

### Requirements

- CMake 3.20 or newer
- C++17-compatible compiler (MSVC, GCC, Clang)
- OpenGL development libraries on Linux

### Clone and build

```bash
git clone https://github.com/hgruntt/MatEdit.git
cd MatEdit
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

On Linux, the project is typically built with X11 enabled and Wayland disabled for compatibility:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_CXX_COMPILER=g++ \
  -DGLFW_BUILD_WAYLAND=OFF \
  -DGLFW_BUILD_X11=ON
cmake --build build --config Release --parallel
```

### Result

The executable is generated in the build directory as `MaterialEditor` (or `MaterialEditor.exe` on Windows).

---

## Nightly builds and CI

The repository includes a GitHub Actions workflow that builds and packages nightly binaries for:

- Linux x64 (GCC)
- Linux x64 (Clang)
- Windows x64 (MSVC)

Nightly artifacts are published as a prerelease named `nightly` and are regenerated automatically on schedule and on pushes to `main`.

---

## Contributors

The project includes a `CONTRIBUTORS.txt` file, and contributor metadata is copied into the app package at build time. Contributions are tracked in the repository history and via the project contributors list.

---

## License

This project is distributed under the GNU General Public License v3.0. See `LICENSE` for full details.

---

## RU

**MatEdit** — это современный настольный редактор для визуализации, создания и редактирования игровых материалов и физических параметров, используемых в проектах на Xash3D / Xash3D FWGS. Поддерживаются рабочие процессы с `.mat` и `.def`, загрузка текстур, настройка параметров материалов и предварительный просмотр в реальном времени.

Проект эволюционировал из базового редактора материалов в кроссплатформенный инструмент с автоматической сборкой, поддержкой контрибьюторов и ночными релизами для Windows и Linux.

---

## Возможности

- **PBR-подобный просмотр материалов** с предварительным просмотром освещения и параметров в реальном времени.
- **Поддержка карт текстур**:
  - Диффузная / albedo
  - Карта нормалей
  - Gloss / specular / AO
  - Карта свечения / emissive
  - Карта высот / bump / parallax
- **Загрузка skybox** через DDS-кубические карты для реалистичных отражений и фона.
- **Редактирование материалов и физики**:
  - Разбор и сохранение `.mat` файлов
  - Редактирование физических свойств материалов (`materials.def` и аналогичных данных)
  - Звуки шагов, ударов, эффекты и decals
- **Интерактивная 3D-сцена**:
  - Куб, сфера, плоскость, цилиндр, конус, тор и чайник
  - Орбитальная камера и гибкие режимы освещения
- **Встроенный шейдерный pipeline** без необходимости хранить внешние шейдеры рядом с исполняемым файлом.
- **Кроссплатформенная сборка** на CMake с автоматической упаковкой nightly-артефактов.
- **Поддержка списка контрибьюторов** в приложении и в пакетах сборки.

---

## Структура проекта

- `src/main.cpp` — точка входа, основной цикл рендеринга, логика камеры и ограничение FPS.
- `src/MaterialSystem.cpp / .h` — разбор материалов, загрузка текстур и работа с физическими свойствами.
- `src/EditorUI.cpp / .h` — интерфейс, файловый браузер, предпросмотры и инструменты редактирования на Dear ImGui.
- `src/Shader.cpp / .h` и `src/ShadersSource.h` — компиляция шейдеров из памяти и управление OpenGL-программами.
- `src/Config.cpp / .h` — конфигурация редактора.
- `src/ImageLoader.cpp` — загрузка изображений и текстур.
- `src/Skybox.cpp` — работа с DDS-кубическими картами.
- `src/PostProcess.cpp` — постпроцессинг.
- `src/Contributors.cpp` — интеграция данных о контрибьюторах.
- `src/shaders/` — исходники шейдеров.
- `external/` — сторонние зависимости: GLFW, GLAD, GLM, GLI, stb, Dear ImGui.
- `cmake/` — вспомогательные CMake-файлы.
- `CONTRIBUTORS.txt` — список участников проекта.

---

## Сборка проекта

### Требования

- CMake 3.20 или новее
- Компилятор с поддержкой C++17 (MSVC, GCC, Clang)
- Для Linux: библиотеки OpenGL и системные зависимости X11

### Клонирование и сборка

```bash
git clone https://github.com/hgruntt/MatEdit.git
cd MatEdit
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

На Linux обычно используют сборку с X11 и отключённым Wayland:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_CXX_COMPILER=g++ \
  -DGLFW_BUILD_WAYLAND=OFF \
  -DGLFW_BUILD_X11=ON
cmake --build build --config Release --parallel
```

### Результат

Исполняемый файл будет создан в каталоге сборки как `MaterialEditor` (или `MaterialEditor.exe` в Windows).

---

## Ночные сборки и CI

В репозитории есть workflow GitHub Actions, который собирает и упаковывает nightly-сборки для:

- Linux x64 (GCC)
- Linux x64 (Clang)
- Windows x64 (MSVC)

Артефакты публикуются как prerelease с тегом `nightly` и пересобираются по расписанию и при каждом пуше в `main`.

---

## Контрибьюторы

В проекте есть файл `CONTRIBUTORS.txt`, а его содержимое используется приложением и попадает в пакеты сборки. История коммитов и список участников отражают текущую структуру сообщества проекта.

---

## Лицензия

Проект распространяется под лицензией GNU General Public License v3.0. Подробности см. в файле `LICENSE`.
