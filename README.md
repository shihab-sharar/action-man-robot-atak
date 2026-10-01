# Action Man Robot Atak

A 2D side-scrolling platformer written in **C** with **[raylib](https://www.raylib.com/)**. Run, jump and fight your way through a huge three-level map, collect coins, defeat patrolling robots and climb the leaderboard.

> This is a fan recreation of the original *Action Man: Robot Atak*, developed by Magic Pockets and published by Atari in 2004. This version was built as a university project and is **not intended for commercial use**.

---

## Features

- **Side-scrolling platformer** across a ~59,000 px wide map with ~75 hand-placed platforms, hanging platforms and stepped "pool" sections
- **Three levels** with a fading "Level Complete" popup between them and a victory screen at the end
- **Enemy AI** with four states: patrol, chase, attack and dying
- **Combat**: fight back during an enemy attack, or sneak up and **backstab** an enemy whose back is turned
- **Moving platforms** that carry the player along their path
- **Coins** with spin and collect animations
- **Health system**: 3 lives, a hurt pose, brief invulnerability after a hit and a death-blink sequence
- **Three difficulty levels** that change how many enemies spawn and where
- **Persistent Top 5 leaderboard** saved to `highscores.txt`
- **Full menu system**: main menu, How to Play, Options (music / SFX toggles), Credits, Start Game screen and an in-game pause menu
- **Music and sound effects** for jumping, coins, hits, defeats, level completion, victory, game over and menu actions
- **Smooth camera** that eases toward the player and never scrolls past the world edges

---

## Controls

| Action | Key |
| --- | --- |
| Run left / right | `Left` / `Right` arrow |
| Jump | `Space` |
| Fight | `X` |
| Pause / resume | `P` or `Esc` (or click the pause button) |
| Menu navigation | `Up` / `Down`, `Enter` to select, or use the mouse |
| Go back (menus) | `Esc` or `Backspace` |
| Pick difficulty (Start Game screen) | `1` Easy, `2` Medium, `3` Hard, or click |
| Skip the Game Over / Game Complete wait | `Enter` |

---

## How to Play

1. On the **Start Game** screen, type your name and choose a difficulty.
2. Press **START!** (or `Enter`).
3. Run right through the map, jump across gaps and platforms, and collect coins.
4. When an enemy spots you it will chase and attack. **Hold `X`** while it attacks to fight back and defeat it. Otherwise you lose a life.
5. Press `X` while standing close behind an enemy that is facing away to **backstab** it instantly.
6. Reach the end of Level 3 to win. If you lose all 3 lives it's game over. Either way your score is saved to the leaderboard.

### Scoring

| Event | Points |
| --- | --- |
| Collect a coin | +10 |
| Defeat an enemy | +50 |

### Difficulty

| Difficulty | Platforms that get enemies | Enemies per platform |
| --- | --- | --- |
| Easy | Width >= 2200 px | 1 |
| Medium | Width >= 1500 px | 1 |
| Hard | Width >= 1000 px | 2 |

### Levels

The map is split by world x-coordinate:

| Level | Range (x) |
| --- | --- |
| 1 | 0 to 20,000 |
| 2 | 20,000 to 40,000 |
| 3 | 40,000 to 59,000 |

---

## Building and Running

This project is developed on **Windows** using **VS Code** with the **MinGW** toolchain (GCC + GDB) and is built and launched by debugging with **F5**.

### Requirements

- Windows with a MinGW GCC toolchain (developed with the MinGW bundled with Code::Blocks)
- [VS Code](https://code.visualstudio.com/) with the **C/C++ extension** (`ms-vscode.cpptools`)
- [raylib](https://github.com/raysan5/raylib) for MinGW (`raylib.h`, `raymath.h` and `libraylib.a` must be reachable by the compiler; `raymath.h` ships with raylib)

### Run from VS Code (recommended)

1. Open the project folder in VS Code.
2. Press **F5**. VS Code compiles the game with the build task from `.vscode/tasks.json`, then starts it under MinGW's `gdb.exe` through the C/C++ extension.
3. The game window opens. Use the debug toolbar to pause, step or stop.

Make sure `"cwd"` in `.vscode/launch.json` is `${workspaceFolder}`. Assets are loaded with relative paths (`assets/...`) and `highscores.txt` is written to the working directory, so the game must run from the project root.

---

## Project Structure

```
.
├── main.c                  # The whole game (single source file)
├── highscores.txt          # Created automatically on the first saved score
└── assets/
    ├── audio/
    │   ├── background.mp3
    │   ├── jump.wav
    │   ├── menu_select.wav
    │   ├── menu_toggle.wav
    │   ├── coin.wav
    │   ├── hurt.wav
    │   ├── enemy_defeat.wav
    │   ├── level_complete.wav
    │   ├── victory.wav
    │   └── game_over.wav
    ├── background/
    │   ├── bg.png
    │   ├── map.png
    │   ├── bush.png
    │   ├── moving-platform.png
    │   ├── menu.png
    │   └── start.png
    └── sprites/
        ├── player-idle-0..7.png
        ├── player-run-0..9.png
        ├── player-jump-0..6.png
        ├── player-defeat-0..9.png
        ├── player-dead-0..8.png
        ├── player-fight-{0..2}-{0..7}.png
        ├── enemy-walk-0..10.png
        ├── enemy-run-0..6.png
        ├── enemy-attack-0..13.png
        ├── coin-spin-0..6.png
        └── coin-collect-0..5.png
```

If a menu background image fails to load, the game falls back to a dark gradient.

---

## Technical Overview

- **Game loop** with delta time (clamped to 1/30 s to avoid huge physics steps after a lag spike)
- **State machines** for both the screens (`GameScreen`) and the enemy AI (`EnemyState`)
- **Physics**: gravity and velocity integration, jump impulse and AABB collision with previous-frame landing checks
- **Moving platforms** use `Vector2Lerp` along a path and carry the rider by the per-frame delta
- **Camera2D** with exponential easing and edge clamping
- **Frame-by-frame sprite animation** with timers, and mirrored drawing for left-facing sprites
- **Data-driven tuning** through `#define` constants, `difficultySettings[]` and `levelBoundaries[]`
- **File I/O** for the high-score table, kept sorted with an insertion sort
- **Immediate-mode UI** for all menus and buttons

### Tweaking the game

| What | Where |
| --- | --- |
| Run speed, jump strength, gravity | `playerSpeedMax`, `playerSpeedUp`, `g` at the top of `main()` |
| Enemy speeds, detection and attack ranges | `ENEMY_*` constants |
| Player health and invulnerability time | `PLAYER_MAX_HEALTH`, `PLAYER_HURT_COOLDOWN` |
| Enemy count per difficulty | `difficultySettings[]` |
| Level lengths | `levelBoundaries[]` |
| Platform layout | the `platform[]` array |
| Moving platform paths | the `mpPaths[]` array |

---

## Known Issues / Ideas

- `srand(time(NULL))` is called every frame while `X` is held, so the fight animation can repeat the same choice. Seed once at startup instead.
- `MAX_COINS` is 256. Adding many more platforms can silently drop coins.
- Each animation frame is a separate texture file. A texture atlas would be more efficient.

---

## Credits

- Original game: *Action Man: Robot Atak*, developed by **Magic Pockets**, published by **Atari** (2004)
- Recreated by **Shihab Sharar** and **Minhajul Abedin Tuhin**
- Built with [raylib](https://www.raylib.com/)

*Developed as a university project. Not for commercial use.*
