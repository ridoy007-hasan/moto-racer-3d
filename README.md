# Moto Racer 3D: Highway Race Simulator

A 3D highway motorbike racing game made with **C++, OpenGL and GLUT**.
Race against 3 AI riders on a curved city highway, avoid traffic, use nitro and kick your rivals.

No external models, textures or sound files are used. Everything is drawn with basic OpenGL shapes (`GL_QUADS`, `GL_POLYGON`).

## Features

- Highway-Rider style chase camera that follows the road bends
- Procedurally curved road with buildings, trees and street lamps
- 50 traffic vehicles (cars and trucks) in three lanes
- 3 AI riders that avoid traffic, use nitro and fight back
- Health, damage, crash and respawn system
- Nitro boost and side kicks
- HUD with speed, nitro, health, ranking board and race progress bar
- Start lights countdown and result medal at the end of the race
- Frame-rate independent movement and smooth steering

## Controls (hold the keys)

| Key | Action |
|---|---|
| W | Speed up |
| S | Brake |
| A | Move left |
| D | Move right |
| Space | Nitro boost |
| Z | Kick left |
| X | Kick right |
| R | Restart the race |
| ESC | Quit |

## How to Compile and Run

**Windows (MinGW with freeglut):**
```
g++ moto_racer_simple.cpp -o moto.exe -lfreeglut -lopengl32 -lglu32
moto.exe
```

**Linux:**
```
g++ moto_racer_simple.cpp -o moto -lGL -lGLU -lglut -lm
./moto
```

**macOS:**
```
g++ moto_racer_simple.cpp -o moto -framework OpenGL -framework GLUT
./moto
```

It can also be built in Code::Blocks by adding the file to a project and linking `freeglut`, `opengl32` and `glu32`.

## Project Files

- `moto_racer_simple.cpp` : full game source code
- `Moto_Racer_Project_Report.pdf` : project report

## Author

Md. Mehedi Hasan Ridoy


Department of Computer Science and Engineering (CSE), BAUET


Course: CSE-4205 Computer Graphics Sessional
