# MatEdit

## ENG

**MatEdit** is a modern desktop editor for visualizing, creating, and editing game material definitions and physical material data used by Xash3D / Xash3D FWGS projects. It supports `.mat` and `.def` workflows, material preview, texture assignment, and real-time preview of lighting and PBR material behavior.

---

## Features

- **PBR-style material preview** with real-time lighting and material parameter visualization.
- **Texture map support**:
  - Albedo / diffuse
  - Normal maps
  - Gloss
  - Luma / emissive maps
  - Height 
- **Skybox environment loading** using DDS cubemaps for realistic reflections and background.
- **Material and physics editing**:
  - Parsing and saving `.mat` files
  - Editing physical material definitions (`materials.def` / similar game data)
  - Sound properties, impact settings, and decals
- **Interactive 3D viewport**:
  - Cube, sphere, plane, cylinder, cone, torus, and teapot primitives
  - Orbit camera controls and adjustable lighting modes
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

## License

This project is distributed under the GNU General Public License v3.0. See `LICENSE` for full details.

---

## RU

**MatEdit** — это современный настольный редактор для визуализации, создания и редактирования игровых материалов и физических параметров, используемых в проектах на Xash3D / Xash3D FWGS. Поддерживаются рабочие процессы с `.mat` и `.def`, загрузка текстур, настройка параметров материалов и предварительный просмотр в реальном времени.

---

## Возможности

- **PBR-подобный просмотр материалов** с предварительным просмотром освещения и параметров в реальном времени.
- **Поддержка карт текстур**:
  - Диффузная / albedo
  - Карта нормалей
  - Gloss 
  - Карта свечения / emissive
  - Карта высот / parallax
- **Загрузка skybox** через DDS-кубические карты для реалистичных отражений и фона.
- **Редактирование материалов и физики**:
  - Разбор и сохранение `.mat` файлов
  - Редактирование физических свойств материалов (`materials.def` и аналогичных данных)
  - Звуки шагов, ударов, эффекты и decals
- **Интерактивная 3D-сцена**:
  - Куб, сфера, плоскость, цилиндр, конус, тор и чайник
  - Орбитальная камера и гибкие режимы освещения

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

## Лицензия

Проект распространяется под лицензией GNU General Public License v3.0. Подробности см. в файле `LICENSE`.
