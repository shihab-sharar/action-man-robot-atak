// =============================================================================
// Action Man Robot Atak
// A 2D side-scrolling platformer built with raylib.
//
// Structure of this file:
//   1. Data types (Coin, MovingPlatform, Enemy)
//   2. Tunable constants (speeds, ranges, timings)
//   3. Difficulty / level / high-score systems
//   4. Small drawing helpers
//   5. main() -- asset loading, then the game loop, which is a state machine
//      over `screen` (menu screens fall straight into their own block and
//      `continue`; only SCREEN_PLAYING falls through to the actual gameplay
//      code at the bottom of the loop).
// =============================================================================

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include "raylib.h"
#include "raymath.h" // for Vector2Zero(), Vector2Lerp(), etc.

// ---------------------------------------------------------------------------
// Coins
// ---------------------------------------------------------------------------
#define COIN_SIZE 180
#define MAX_COINS 256          // comfortably covers every platform's coins (~3 per platform)
#define COIN_FRAME_TIME 0.08f

typedef struct Coin {
    Vector2 pos;       // top-left corner, world space
    bool active;       // still drawn / collectible
    bool collecting;   // true while playing the pickup animation
    int frame;
    float timer;
} Coin;

// ---------------------------------------------------------------------------
// Moving platforms
// ---------------------------------------------------------------------------
#define MP_W 395
#define MP_H 110

typedef struct MovingPlatform {
    Vector2 start, end;   // top-left corner at each end of the path
    Vector2 pos;          // current top-left corner
    Vector2 delta;        // how far it moved this frame (used to carry the player)
    float speed;          // pixels per second
    float t;              // 0..1 progress along the path
    int dir;              // +1 heading toward `end`, -1 heading toward `start`
} MovingPlatform;

// ---------------------------------------------------------------------------
// Enemies
// ---------------------------------------------------------------------------
typedef enum { ENEMY_PATROL, ENEMY_CHASE, ENEMY_ATTACK, ENEMY_DYING } EnemyState;

typedef struct Enemy {
    Vector2 pos;                  // feet position, same convention as the player
    float patrolMinX, patrolMaxX; // how far it walks back and forth while patrolling
    int facing;                   // -1 left, +1 right
    EnemyState state;
    Texture2D *sprites;            // current animation set (walk/run/attack)
    int frameCount;
    int frame;
    float animTimer;
    float attackTimer;             // counts down to the next "hit" while attacking
    bool fightFlag;                 // true if the player pressed X anytime during the
                                     // current attack window (see ENEMY_ATTACK below)
    float blinkTimer;
    int blinkCount;
    bool visible;                   // toggled on/off while dying, for the blink effect
    bool alive;
} Enemy;

#define MAX_ENEMIES 128

#define ENEMY_DETECT_RANGE    700.0f   // must be this close (and facing) to start a chase
#define ENEMY_LOSE_RANGE      900.0f   // give up the chase beyond this
#define ENEMY_ATTACK_RANGE    90.0f    // close enough to start attacking
#define ENEMY_PATROL_SPEED    300.0f
#define ENEMY_RUN_SPEED       500.0f
#define ENEMY_ATTACK_INTERVAL 0.9f     // seconds between "hits" while attacking
#define ENEMY_ANIM_FRAME_TIME 0.07f
#define BLINK_TIME             0.12f
#define BLINK_COUNT             8

#define ENEMY_KILL_POINTS     50   // score awarded for defeating an enemy

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------
#define PLAYER_MAX_HEALTH     3
#define PLAYER_DEFEAT_TIME    0.6f    // how long the "just got hit" pose plays
#define PLAYER_HURT_COOLDOWN  1.0f    // brief invulnerability window after being hit

// ---------------------------------------------------------------------------
// Difficulty levels: control how many enemies get spawned and where
// ---------------------------------------------------------------------------
typedef enum { DIFF_EASY, DIFF_MEDIUM, DIFF_HARD, DIFF_COUNT } Difficulty;

typedef struct DifficultySettings {
    float minPlatformWidth;   // platforms at least this wide get enemies
    int enemiesPerPlatform;   // enemies placed per qualifying platform
} DifficultySettings;

static const DifficultySettings difficultySettings[DIFF_COUNT] = {
    { 2200.0f, 1 },  // Easy   - only the widest platforms, one enemy each
    { 1500.0f, 1 },  // Medium - more platforms qualify, one enemy each
    { 1000.0f, 2 },  // Hard   - even more platforms qualify, two enemies each
};

static const char *difficultyNames[DIFF_COUNT] = { "Easy", "Medium", "Hard" };

// ---------------------------------------------------------------------------
// Screens: the whole game is one state machine over these values
// ---------------------------------------------------------------------------
typedef enum {
    SCREEN_MAIN_MENU,
    SCREEN_HOW_TO_PLAY,
    SCREEN_OPTIONS,
    SCREEN_CREDITS,
    SCREEN_START_GAME,   // name entry + difficulty picker + leaderboard + START! button
    SCREEN_PLAYING
} GameScreen;

#define GAME_OVER_DELAY 1.5f   // seconds the Game Over / Game Complete overlay stays up
                               // before auto-returning to the Start Game screen

// ---------------------------------------------------------------------------
// Levels: the map is split into 3 segments by world x-coordinate
// ---------------------------------------------------------------------------
#define LEVEL_COUNT 3
#define LEVEL_POPUP_DURATION 3.0f   // total seconds the "Level Complete" popup stays up
#define LEVEL_POPUP_FADE     0.5f   // seconds of fade-in / fade-out at each end

// Level i spans [levelBoundaries[i], levelBoundaries[i+1]).
// Tune these to line up with natural break points in your map.
static const float levelBoundaries[LEVEL_COUNT + 1] = { 0.0f, 20000.0f, 40000.0f, 59000.0f };

// ---------------------------------------------------------------------------
// Small drawing helpers
// ---------------------------------------------------------------------------

// Draws text centered horizontally on `centerX`.
static void DrawCenteredText(const char *text, int centerX, int y, int fontSize, Color color) {
    int w = MeasureText(text, fontSize);
    DrawText(text, centerX - w / 2, y, fontSize, color);
}

// Same as above, but with a soft drop shadow (used for screen titles).
static void DrawCenteredTextShadow(const char *text, int centerX, int y, int fontSize, Color color) {
    DrawCenteredText(text, centerX + 3, y + 3, fontSize, Fade(BLACK, 0.6f));
    DrawCenteredText(text, centerX, y, fontSize, color);
}

// Draws a full-screen stretched background texture (or a fallback gradient if the
// texture failed to load) plus a dark overlay of the given alpha for text contrast.
static void DrawScreenBackground(Texture2D tex, float screenWidth, float screenHeight, float overlayAlpha) {
    if (tex.id != 0) {
        DrawTexturePro(tex, (Rectangle){0, 0, (float)tex.width, (float)tex.height},
                        (Rectangle){0, 0, screenWidth, screenHeight}, (Vector2){0, 0}, 0, WHITE);
    } else {
        ClearBackground((Color){18, 18, 26, 255});
        DrawRectangleGradientV(0, 0, (int)screenWidth, (int)screenHeight,
                                (Color){18, 18, 26, 255}, (Color){45, 30, 60, 255});
    }
    if (overlayAlpha > 0.0f) DrawRectangle(0, 0, (int)screenWidth, (int)screenHeight, Fade(BLACK, overlayAlpha));
}

// ---------------------------------------------------------------------------
// Persistent high scores: top 5 entries, saved to highscores.txt next to the
// executable so they survive between play sessions.
// ---------------------------------------------------------------------------
#define MAX_HIGH_SCORES 5

typedef struct HighScore {
    char name[16];
    int score;
} HighScore;

// Reads up to MAX_HIGH_SCORES "name score" lines from highscores.txt.
// Safe to call even if the file doesn't exist yet (first run).
static void LoadHighScores(HighScore *list, int *count) {
    *count = 0;
    FILE *f = fopen("highscores.txt", "r");
    if (!f) return;
    char name[16];
    int score;
    while (*count < MAX_HIGH_SCORES && fscanf(f, "%15s %d", name, &score) == 2) {
        strncpy(list[*count].name, name, sizeof(list[*count].name) - 1);
        list[*count].name[sizeof(list[*count].name) - 1] = '\0';
        list[*count].score = score;
        (*count)++;
    }
    fclose(f);
}

// Overwrites highscores.txt with the current list.
static void SaveHighScores(HighScore *list, int count) {
    FILE *f = fopen("highscores.txt", "w");
    if (!f) return;
    for (int i = 0; i < count; i++) fprintf(f, "%s %d\n", list[i].name, list[i].score);
    fclose(f);
}

// Inserts a new score into the (already sorted, descending) list, dropping the
// lowest entry if the list is already full and the new score doesn't beat it.
static void InsertHighScore(HighScore *list, int *count, const char *name, int score) {
    int idx;
    if (*count < MAX_HIGH_SCORES) {
        idx = *count;
        (*count)++;
    } else {
        int lowestIdx = 0;
        for (int i = 1; i < MAX_HIGH_SCORES; i++) {
            if (list[i].score < list[lowestIdx].score) lowestIdx = i;
        }
        if (score <= list[lowestIdx].score) return; // doesn't make the cut
        idx = lowestIdx;
    }
    strncpy(list[idx].name, name, sizeof(list[idx].name) - 1);
    list[idx].name[sizeof(list[idx].name) - 1] = '\0';
    list[idx].score = score;

    // Simple insertion sort, descending by score (only ever MAX_HIGH_SCORES entries,
    // so this is plenty fast).
    for (int i = 1; i < *count; i++) {
        HighScore key = list[i];
        int j = i - 1;
        while (j >= 0 && list[j].score < key.score) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = key;
    }
}


int main(void)
{
    // ------------------------------- Window / audio setup -------------------------------
    float screenHeight = 800;
    float screenWidth = 1200;
    float playerSpeedMax = 550;   // horizontal run speed
    float playerSpeedUp = 1400;   // upward speed applied on jump
    float g = 2000;               // gravity

    InitWindow(screenWidth, screenHeight, "Action Man Robot Atak");
    SetTargetFPS(60);
    InitAudioDevice();

    Music musicbg = LoadMusicStream("assets/audio/background.mp3");
    musicbg.looping = true;

    Sound jumpSound          = LoadSound("assets/audio/jump.wav");
    Sound menuSelectSound    = LoadSound("assets/audio/menu_select.wav");
    Sound menuToggleSound    = LoadSound("assets/audio/menu_toggle.wav");
    Sound coinSound          = LoadSound("assets/audio/coin.wav");
    Sound hurtSound          = LoadSound("assets/audio/hurt.wav");
    Sound enemyDefeatSound   = LoadSound("assets/audio/enemy_defeat.wav");
    Sound levelCompleteSound = LoadSound("assets/audio/level_complete.wav");
    Sound victorySound       = LoadSound("assets/audio/victory.wav");
    Sound gameOverSound      = LoadSound("assets/audio/game_over.wav");

    // ------------------------------- Background textures -------------------------------
    Texture2D bg             = LoadTexture("assets/background/bg.png");
    Texture2D map            = LoadTexture("assets/background/map.png");
    Texture2D bush           = LoadTexture("assets/background/bush.png");
    Texture2D movingPlatform = LoadTexture("assets/background/moving-platform.png");
    Texture2D menuBg         = LoadTexture("assets/background/menu.png");
    Texture2D startBg        = LoadTexture("assets/background/start.png");

    // ------------------------------- Player sprite sheets -------------------------------
    Texture2D playerIdle[8];
    for (int i = 0; i < 8; i++) {
        char path[50];
        sprintf(path, "assets/sprites/player-idle-%d.png", i);
        playerIdle[i] = LoadTexture(path);
    }
    Texture2D playerRun[10];
    for (int i = 0; i < 10; i++) {
        char path[50];
        sprintf(path, "assets/sprites/player-run-%d.png", i);
        playerRun[i] = LoadTexture(path);
    }
    Texture2D playerJump[7];
    for (int i = 0; i < 7; i++) {
        char path[50];
        sprintf(path, "assets/sprites/player-jump-%d.png", i);
        playerJump[i] = LoadTexture(path);
    }
    Texture2D playerDefeat[10];
    for (int i = 0; i < 10; i++) {
        char path[50];
        sprintf(path, "assets/sprites/player-defeat-%d.png", i);
        playerDefeat[i] = LoadTexture(path);
    }
    Texture2D playerDead[9];
    for (int i = 0; i < 9; i++) {
        char path[50];
        sprintf(path, "assets/sprites/player-dead-%d.png", i);
        playerDead[i] = LoadTexture(path);
    }
    Texture2D playerFight[3][8];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 8; j++) {
            char path[50];
            sprintf(path, "assets/sprites/player-fight-%d-%d.png", i, j);
            playerFight[i][j] = LoadTexture(path);
        }
    }

    // ------------------------------- Enemy sprite sheets -------------------------------
    Texture2D enemyWalk[11];
    for (int i = 0; i < 11; i++) {
        char path[50];
        sprintf(path, "assets/sprites/enemy-walk-%d.png", i);
        enemyWalk[i] = LoadTexture(path);
    }
    Texture2D enemyRun[7];
    for (int i = 0; i < 7; i++) {
        char path[50];
        sprintf(path, "assets/sprites/enemy-run-%d.png", i);
        enemyRun[i] = LoadTexture(path);
    }
    Texture2D enemyAttack[14];
    for (int i = 0; i < 14; i++) {
        char path[50];
        sprintf(path, "assets/sprites/enemy-attack-%d.png", i);
        enemyAttack[i] = LoadTexture(path);
    }

    // ------------------------------- Coin sprite sheets -------------------------------
    Texture2D coinSpin[7];
    for (int i = 0; i < 7; i++) {
        char path[50];
        sprintf(path, "assets/sprites/coin-spin-%d.png", i);
        coinSpin[i] = LoadTexture(path);
    }
    Texture2D coinCollect[6];
    for (int i = 0; i < 6; i++) {
        char path[50];
        sprintf(path, "assets/sprites/coin-collect-%d.png", i);
        coinCollect[i] = LoadTexture(path);
    }

    // ------------------------------- Level layout (static platforms) -------------------------------
    // `x` shrinks each platform's collision box slightly on both sides so the
    // player can't clip against invisible edges.
    int x = 75;
    Rectangle platform[] = {
        (Rectangle) {0 + x, 2955, 2010 - 2*x, 155},
        (Rectangle) {2010 + x, 2555, 880 -  2*x, 555},
        (Rectangle) {2890 + x, 2155, 1200 - 2*x, 955},
        (Rectangle) {4090 + x, 1755, 1520 - 2*x, 1355},
        (Rectangle) {5610 + x, 1355, 1100 - 2*x, 1755},
        (Rectangle) {6710 + x, 1675, 960 - 2*x, 1435},
        (Rectangle) {7670 + x, 2075, 2420 - 2*x, 1035},
        (Rectangle) {9260 + x, 1755, 580 - 2*x, 50}, //platform 1
        (Rectangle) {10090 + x, 1355, 940 - 2*x, 1755},
        (Rectangle) {11030 + x, 1430, 100 - 2*x, 1680},
        (Rectangle) {11130 + x, 1470, 150 - 2*x, 1680},
        (Rectangle) {11280 + x, 1500, 610 - 2*x, 1680},
        (Rectangle) {11890 + x, 1470, 150 - 2*x, 1680},
        (Rectangle) {12040 + x, 1430, 100 - 2*x, 1680},
        (Rectangle) {12140 + x, 1350, 390 - 2*x, 1680},
        (Rectangle) {12500 + x, 1515, 550 - 2*x, 1680},
        (Rectangle) {13050 + x, 1115, 1660 - 2*x, 1680},
        (Rectangle) {14040 + x, 795, 520 - 2*x, 70}, //platform 2
        (Rectangle) {14710 + x, 1515, 5060 - 2*x, 1680},
        (Rectangle) {16410 + x, 715, 860 - 2*x, 500}, //matir vitorer rasta
        (Rectangle) {15790 + x, 875, 520 - 2*x, 70}, //platform 2
        (Rectangle) {17550 + x, 795, 540 - 2*x, 70}, //platform 2 hanging
        (Rectangle) {18430 + x, 795, 380 - 2*x, 70}, //platform 2 hanging
        (Rectangle) {18880 + x, 1195, 560 - 2*x, 50}, //platform 1
        (Rectangle) {19770 + x, 1115, 1500 - 2*x, 1680},
        (Rectangle) {21270 + x, 1515, 1300 - 2*x, 1680},
        (Rectangle) {22570 + x, 1115, 2460 - 2*x, 1680},
        (Rectangle) {25030 + x, 1435, 1600 - 2*x, 1680},
        (Rectangle) {25390 + x, 1035, 540 - 2*x, 70}, //platform 2
        (Rectangle) {26630 + x, 1915, 640 - 2*x, 1680},
        (Rectangle) {27270 + x, 2215, 4340 - 2*x, 1680},
        (Rectangle) {28190 + x, 1275, 540 - 2*x, 70}, //platform 2 hanging
        (Rectangle) {30030 + x, 1275, 1260 - 2*x, 70}, //platform 2 hanging
        (Rectangle) {31610 + x, 1515, 480 - 2*x, 1680},
        (Rectangle) {32090 + x, 1115, 2140 - 2*x, 1680},
        (Rectangle) {34230 + x, 1435, 1760 - 2*x, 1680},
        (Rectangle) {35990 + x, 1835, 5940 - 2*x, 1680},
        (Rectangle) {38180 + x, 1515, 530 - 2*x, 50}, //platform 1
        (Rectangle) {39550 + x, 880, 1110 - 2*x, 70}, //platform 2 hanging
        (Rectangle) {41070 + x, 1435, 540 - 2*x, 70}, //platform 2
        (Rectangle) {41930 + x, 1195, 480 - 2*x, 1680},
        (Rectangle) {42410 + x, 795, 1660 - 2*x, 1680},
        (Rectangle) {44070 + x, 1195, 1200 - 2*x, 1680},
        (Rectangle) {45270 + x, 1595, 1120 - 2*x, 1680},
        (Rectangle) {46390 + x, 1665, 125 - 2*x, 50}, //pool
        (Rectangle) {46515 + x, 1730, 205 - 2*x, 50}, //pool
        (Rectangle) {46960 + x, 1800, 130 - 2*x, 50}, //pool
        (Rectangle) {47090 + x, 1890, 290 - 2*x, 50}, //pool
        (Rectangle) {47570 + x, 1900, 170 - 2*x, 50}, //pool
        (Rectangle) {47740 + x, 1830, 130 - 2*x, 50}, //pool
        (Rectangle) {47870 + x, 1740, 200 - 2*x, 50}, //pool
        (Rectangle) {48070 + x, 1670, 120 - 2*x, 50}, //pool
        (Rectangle) {48190 + x, 1600, 230 - 2*x, 50}, //pool
        (Rectangle) {48610 + x, 1600, 430 - 2*x, 50}, //pool
        (Rectangle) {46390 + x, 2635, 2900 - 2*x, 1680},
        (Rectangle) {49290 + x, 1675, 1660 - 2*x, 1680},
        (Rectangle) {49790 + x, 875, 790 - 2*x, 70}, //platform 2 hanging
        (Rectangle) {50950 + x, 2075, 1040 - 2*x, 1680},
        (Rectangle) {51230 + x, 955, 620 - 2*x, 70}, //platform 2 hanging
        (Rectangle) {51990 + x, 2475, 5540 - 2*x, 1680},
        (Rectangle) {52920 + x, 795, 1820 - 2*x, 70}, //platform 2 hanging
        (Rectangle) {54960 + x, 970, 470 - 2*x, 50}, //pool
        (Rectangle) {55430 + x, 1040, 130 - 2*x, 50}, //pool
        (Rectangle) {55560 + x, 1100, 220 - 2*x, 50}, //pool
        (Rectangle) {55890 + x, 1130, 280 - 2*x, 50}, //pool
        (Rectangle) {56170 + x, 1200, 120 - 2*x, 50}, //pool
        (Rectangle) {56290 + x, 1270, 270 - 2*x, 50}, //pool
        (Rectangle) {56690 + x, 1310, 150 - 2*x, 50}, //pool
        (Rectangle) {56840 + x, 1400, 140 - 2*x, 50}, //pool
        (Rectangle) {56980 + x, 1440, 340 - 2*x, 50}, //pool
        (Rectangle) {57320 + x, 1360, 210 - 2*x, 50}, //pool
        (Rectangle) {57530 + x, 1275, 3430 - 2*x, 1680},
    };
    int platformNum = sizeof(platform) / sizeof(platform[0]);

    // Enemies live in one big pool; how many are actually spawned into it (and where)
    // is decided fresh each time the player presses START! on the Start Game screen,
    // based on the chosen difficulty (see difficultySettings above).
    Enemy enemies[MAX_ENEMIES];
    int enemyCount = 0;
    float enemyEdgeMargin = 150.0f;   // keeps enemies from patrolling off a platform's edge

    // ------------------------------- Moving platforms -------------------------------
    Vector2 mpPaths[5][2] = {
        {{27400, 1200}, {27400, 1900}},
        {{28800, 1275}, {29500, 1275}},
        {{38800,  900}, {38800, 1300}},
        {{52100,  800}, {52100, 2000}},
        {{48800, 2000}, {48800, 2550}},
    };
    int mpCount = 5;
    MovingPlatform mplat[5];
    for (int i = 0; i < mpCount; i++) {
        mplat[i] = (MovingPlatform){
            .start = mpPaths[i][0], .end = mpPaths[i][1],
            .pos = mpPaths[i][0], .delta = Vector2Zero(),
            .speed = 250, .t = 0, .dir = 1
        };
    }

    // ------------------------------- Coins -------------------------------
    Coin coins[MAX_COINS];
    int coinCount = 0;
    int coinsPerPlatform = 3;
    int score = 0;
    int coinsCollected = 0;
    for (int i = 0; i < platformNum; i++) {
        for (int j = 0; j < coinsPerPlatform && coinCount < MAX_COINS; j++) {
            float t = (j + 1.0f) / (coinsPerPlatform + 1.0f);   // spread evenly across the platform
            coins[coinCount].pos = (Vector2){
                platform[i].x + platform[i].width * t - COIN_SIZE / 2.0f,
                platform[i].y - COIN_SIZE - 30                   // float a bit above the platform
            };
            coins[coinCount].active     = true;
            coins[coinCount].collecting = false;
            coins[coinCount].frame      = 0;
            coins[coinCount].timer      = 0.0f;
            coinCount++;
        }
    }

    // ------------------------------- Player state -------------------------------
    bool flip = false;                  // true = facing left (sprite drawn mirrored)
    int spriteIndex = 0;                // current animation frame
    Texture2D *prevAnimSprites = NULL;  // last frame's animation set, so we can detect
    float playerAnimTimer = 0.0f;       // a switch and restart the animation at frame 0
    Texture2D *sprites = playerIdle;    // current animation set (idle/run/jump/etc.)

    Vector2 playerPosition = {365, 2955};   // player's feet position, world space
    Vector2 playerSpeed = Vector2Zero();
    Vector2 gravity = {0, g};
    Vector2 prevPosition = playerPosition;  // last frame's position, used for landing checks

    Camera2D camera = {0};
    camera.target = playerPosition;
    camera.offset = (Vector2) {365, 650};
    camera.zoom = 1.0f;
    camera.rotation = 0.0f;

    bool isGrounded = true;
    int riding = -1;   // index of the moving platform the player is standing on, or -1

    int playerHealth = PLAYER_MAX_HEALTH;
    bool playerDefeated = false;      // currently playing the "just got hit" pose
    float playerDefeatTimer = 0;
    float playerHurtCooldown = 0;     // brief invulnerability after being hit
    bool playerDying = false;         // health hit 0; playing the death-blink sequence
    float playerDeathBlinkTimer = 0;
    int playerDeathBlinkCount = 0;
    bool playerVisible = true;
    bool gameOver = false;            // true once the death-blink sequence finishes

    // ------------------------------- Level progression state -------------------------------
    bool levelFinished[LEVEL_COUNT] = { false, false, false };
    int levelPopupToShow = -1;   // which level (0-indexed) just finished, for the popup
    float levelPopupTimer = 0.0f;
    bool gameWon = false;        // true once all 3 levels are finished

    // ------------------------------- Screen / menu state -------------------------------
    GameScreen screen = SCREEN_MAIN_MENU;
    Difficulty difficulty = DIFF_MEDIUM;

    int mainMenuSelection = 0;   // 0=START GAME, 1=HOW TO PLAY, 2=OPTIONS, 3=CREDITS
    int optionsSelection = 0;    // 0=MUSIC, 1=SFX
    int pauseMenuSelection = 0;  // 0=RESUME, 1=MAIN MENU
    bool paused = false;
    bool musicOn = true;
    bool sfxOn = true;

    HighScore highScores[MAX_HIGH_SCORES];
    int highScoreCount = 0;
    LoadHighScores(highScores, &highScoreCount);

    // The player types their name once, on the Start Game screen, before pressing
    // START!; it's reused to record this run's score (win or lose) without asking again.
    char playerNameBuf[16] = "";
    int playerNameLen = 0;
    bool scoreRecorded = false;   // guards against saving the same run's score twice
    float gameOverTimer = 0.0f;   // counts down while the Game Over / Game Won overlay is shown

    PlayMusicStream(musicbg);

    // =========================================================================
    // Main game loop
    // =========================================================================
    while (!WindowShouldClose())
    {
        UpdateMusicStream(musicbg);
        float dt = GetFrameTime();
        if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;   // never simulate more than ~33ms per frame
                                                      // (avoids huge physics steps after a lag spike)

        // ----------------------------------------------------------------
        // SCREEN: Main Menu
        // ----------------------------------------------------------------
        if (screen == SCREEN_MAIN_MENU) {
            const char *menuItems[4] = { "START GAME", "HOW TO PLAY", "OPTIONS", "CREDITS" };
            int itemCount = 4;
            Rectangle itemRects[4];
            float itemW = 380, itemH = 60, itemGap = 18;
            float startY = screenHeight / 2 - (itemCount * itemH + (itemCount - 1) * itemGap) / 2.0f;
            Vector2 mouse = GetMousePosition();
            int prevMainMenuSelection = mainMenuSelection;
            for (int i = 0; i < itemCount; i++) {
                itemRects[i] = (Rectangle){ screenWidth / 2 - itemW / 2, startY + i * (itemH + itemGap), itemW, itemH };
                if (CheckCollisionPointRec(mouse, itemRects[i])) mainMenuSelection = i;
            }
            if (mainMenuSelection != prevMainMenuSelection && sfxOn) PlaySound(menuSelectSound);

            if (IsKeyPressed(KEY_DOWN)) { mainMenuSelection = (mainMenuSelection + 1) % itemCount; if (sfxOn) PlaySound(menuSelectSound); }
            if (IsKeyPressed(KEY_UP))   { mainMenuSelection = (mainMenuSelection - 1 + itemCount) % itemCount; if (sfxOn) PlaySound(menuSelectSound); }

            bool activated = IsKeyPressed(KEY_ENTER) ||
                              (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mouse, itemRects[mainMenuSelection]));
            if (activated) {
                if (sfxOn) PlaySound(menuSelectSound);
                if (mainMenuSelection == 0) screen = SCREEN_START_GAME;
                else if (mainMenuSelection == 1) screen = SCREEN_HOW_TO_PLAY;
                else if (mainMenuSelection == 2) screen = SCREEN_OPTIONS;
                else if (mainMenuSelection == 3) screen = SCREEN_CREDITS;
            }

            BeginDrawing();
                DrawScreenBackground(menuBg, screenWidth, screenHeight, 0.45f);
                DrawCenteredTextShadow("ACTION MAN ROBOT ATAK", (int)screenWidth / 2, 90, 52, GOLD);
                for (int i = 0; i < itemCount; i++) {
                    bool sel = (i == mainMenuSelection);
                    DrawRectangleRounded(itemRects[i], 0.3f, 10, sel ? (Color){235, 165, 55, 255} : Fade(BLACK, 0.5f));
                    DrawRectangleRoundedLines(itemRects[i], 0.3f, 10, sel ? GOLD : RAYWHITE);
                    DrawCenteredText(menuItems[i], (int)(itemRects[i].x + itemRects[i].width / 2),
                                     (int)(itemRects[i].y + 16), 28, sel ? BLACK : RAYWHITE);
                }
                DrawCenteredText("Use UP/DOWN and ENTER, or click", (int)screenWidth / 2,
                                 (int)(itemRects[itemCount - 1].y + itemH + 40), 20, GRAY);
            EndDrawing();
            continue;
        }

        // ----------------------------------------------------------------
        // SCREEN: How To Play
        // ----------------------------------------------------------------
        if (screen == SCREEN_HOW_TO_PLAY) {
            Rectangle backBtn = { 20, 20, 140, 44 };
            bool hoverBack = CheckCollisionPointRec(GetMousePosition(), backBtn);
            if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE) || (hoverBack && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))) {
                if (sfxOn) PlaySound(menuSelectSound);
                screen = SCREEN_MAIN_MENU;
            }

            BeginDrawing();
                DrawScreenBackground(menuBg, screenWidth, screenHeight, 0.6f);
                DrawCenteredTextShadow("HOW TO PLAY", (int)screenWidth / 2, 90, 46, GOLD);
                DrawCenteredText("PRESS LEFT AND RIGHT ARROW KEY FOR RUNNING", (int)screenWidth / 2, 220, 26, RAYWHITE);
                DrawCenteredText("PRESS SPACE KEY FOR JUMPING", (int)screenWidth / 2, 270, 26, RAYWHITE);
                DrawCenteredText("PRESS X TO FIGHT WITH THE ENEMY", (int)screenWidth / 2, 320, 26, RAYWHITE);

                DrawRectangleRounded(backBtn, 0.3f, 8, hoverBack ? (Color){90, 90, 110, 255} : (Color){60, 60, 75, 255});
                DrawRectangleRoundedLines(backBtn, 0.3f, 8, RAYWHITE);
                DrawCenteredText("< BACK", (int)(backBtn.x + backBtn.width / 2), (int)(backBtn.y + 12), 20, RAYWHITE);
            EndDrawing();
            continue;
        }

        // ----------------------------------------------------------------
        // SCREEN: Options (music / SFX toggles)
        // ----------------------------------------------------------------
        if (screen == SCREEN_OPTIONS) {
            Rectangle backBtn = { 20, 20, 140, 44 };
            bool hoverBack = CheckCollisionPointRec(GetMousePosition(), backBtn);
            if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE) || (hoverBack && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))) {
                if (sfxOn) PlaySound(menuSelectSound);
                screen = SCREEN_MAIN_MENU;
            }

            Rectangle musicRow = { screenWidth / 2 - 200, screenHeight / 2 - 70, 400, 60 };
            Rectangle sfxRow   = { screenWidth / 2 - 200, screenHeight / 2 + 10, 400, 60 };
            Vector2 mouse = GetMousePosition();
            bool hoverMusic = CheckCollisionPointRec(mouse, musicRow);
            bool hoverSfx   = CheckCollisionPointRec(mouse, sfxRow);
            int prevOptionsSelection = optionsSelection;
            if (hoverMusic) optionsSelection = 0;
            if (hoverSfx)   optionsSelection = 1;
            if (optionsSelection != prevOptionsSelection && sfxOn) PlaySound(menuSelectSound);

            if (IsKeyPressed(KEY_DOWN)) { optionsSelection = (optionsSelection + 1) % 2; if (sfxOn) PlaySound(menuSelectSound); }
            if (IsKeyPressed(KEY_UP))   { optionsSelection = (optionsSelection - 1 + 2) % 2; if (sfxOn) PlaySound(menuSelectSound); }

            bool toggle = IsKeyPressed(KEY_ENTER) ||
                          (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
                           ((optionsSelection == 0 && hoverMusic) || (optionsSelection == 1 && hoverSfx)));
            if (toggle) {
                if (optionsSelection == 0) {
                    musicOn = !musicOn;
                    if (musicOn) ResumeMusicStream(musicbg); else PauseMusicStream(musicbg);
                } else {
                    sfxOn = !sfxOn;
                }
                if (sfxOn) PlaySound(menuToggleSound); // confirms the new state (silent if SFX just turned off)
            }

            BeginDrawing();
                DrawScreenBackground(menuBg, screenWidth, screenHeight, 0.6f);
                DrawCenteredTextShadow("OPTIONS", (int)screenWidth / 2, 90, 46, GOLD);

                DrawRectangleRounded(musicRow, 0.25f, 8, optionsSelection == 0 ? (Color){235, 165, 55, 255} : Fade(BLACK, 0.5f));
                DrawRectangleRoundedLines(musicRow, 0.25f, 8, RAYWHITE);
                DrawCenteredText(TextFormat("MUSIC: %s", musicOn ? "ON" : "OFF"),
                                 (int)(musicRow.x + musicRow.width / 2), (int)(musicRow.y + 16), 28,
                                 optionsSelection == 0 ? BLACK : RAYWHITE);

                DrawRectangleRounded(sfxRow, 0.25f, 8, optionsSelection == 1 ? (Color){235, 165, 55, 255} : Fade(BLACK, 0.5f));
                DrawRectangleRoundedLines(sfxRow, 0.25f, 8, RAYWHITE);
                DrawCenteredText(TextFormat("SFX: %s", sfxOn ? "ON" : "OFF"),
                                 (int)(sfxRow.x + sfxRow.width / 2), (int)(sfxRow.y + 16), 28,
                                 optionsSelection == 1 ? BLACK : RAYWHITE);

                DrawCenteredText("UP/DOWN to select, ENTER or click to toggle", (int)screenWidth / 2,
                                 (int)(sfxRow.y + sfxRow.height + 40), 20, GRAY);

                DrawRectangleRounded(backBtn, 0.3f, 8, hoverBack ? (Color){90, 90, 110, 255} : (Color){60, 60, 75, 255});
                DrawRectangleRoundedLines(backBtn, 0.3f, 8, RAYWHITE);
                DrawCenteredText("< BACK", (int)(backBtn.x + backBtn.width / 2), (int)(backBtn.y + 12), 20, RAYWHITE);
            EndDrawing();
            continue;
        }

        // ----------------------------------------------------------------
        // SCREEN: Credits
        // ----------------------------------------------------------------
        if (screen == SCREEN_CREDITS) {
            Rectangle backBtn = { 20, 20, 140, 44 };
            bool hoverBack = CheckCollisionPointRec(GetMousePosition(), backBtn);
            if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE) || (hoverBack && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))) {
                if (sfxOn) PlaySound(menuSelectSound);
                screen = SCREEN_MAIN_MENU;
            }

            BeginDrawing();
                DrawScreenBackground(menuBg, screenWidth, screenHeight, 0.6f);
                DrawCenteredTextShadow("CREDITS", (int)screenWidth / 2, 90, 46, GOLD);
                DrawCenteredText("ORIGINALLY DEVELOPED BY MAGIC POCKETS AND PUBLISHED BY ATARI IN 2004.",
                                 (int)screenWidth / 2, 240, 22, RAYWHITE);
                DrawCenteredText("RECREATED BY SHIHAB SHARAR & MINHAJUL ABEDIN TUHIN.",
                                 (int)screenWidth / 2, 280, 22, RAYWHITE);
                DrawCenteredText("(THIS VERSION OF THE GAME WAS DEVELOPED AS A UNIVERSITY PROJECT",
                                 (int)screenWidth / 2, 330, 18, LIGHTGRAY);
                DrawCenteredText("AND IS NOT INTENDED FOR COMMERCIAL USE.)",
                                 (int)screenWidth / 2, 356, 18, LIGHTGRAY);

                DrawRectangleRounded(backBtn, 0.3f, 8, hoverBack ? (Color){90, 90, 110, 255} : (Color){60, 60, 75, 255});
                DrawRectangleRoundedLines(backBtn, 0.3f, 8, RAYWHITE);
                DrawCenteredText("< BACK", (int)(backBtn.x + backBtn.width / 2), (int)(backBtn.y + 12), 20, RAYWHITE);
            EndDrawing();
            continue;
        }

        // ----------------------------------------------------------------
        // SCREEN: Start Game (name entry + difficulty + leaderboard + START!)
        // ----------------------------------------------------------------
        if (screen == SCREEN_START_GAME) {
            Rectangle backBtn = { 20, 20, 140, 44 };
            bool hoverBack = CheckCollisionPointRec(GetMousePosition(), backBtn);
            if (IsKeyPressed(KEY_ESCAPE) || (hoverBack && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))) {
                if (sfxOn) PlaySound(menuSelectSound);
                screen = SCREEN_MAIN_MENU;
            }

            // ---- Name entry (typed anytime while on this page) ----
            int ch = GetCharPressed();
            while (ch > 0) {
                if (ch >= 32 && ch <= 125 && playerNameLen < (int)sizeof(playerNameBuf) - 1) {
                    playerNameBuf[playerNameLen++] = (char)ch;
                    playerNameBuf[playerNameLen] = '\0';
                }
                ch = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE) && playerNameLen > 0) {
                playerNameLen--;
                playerNameBuf[playerNameLen] = '\0';
            }

            // ---- Difficulty buttons ----
            Rectangle easyBtn   = { screenWidth / 2 - 350, 300, 220, 56 };
            Rectangle mediumBtn = { screenWidth / 2 - 110, 300, 220, 56 };
            Rectangle hardBtn   = { screenWidth / 2 + 130, 300, 220, 56 };
            Vector2 mouse = GetMousePosition();
            bool hoverEasy   = CheckCollisionPointRec(mouse, easyBtn);
            bool hoverMedium = CheckCollisionPointRec(mouse, mediumBtn);
            bool hoverHard   = CheckCollisionPointRec(mouse, hardBtn);
            bool clicked     = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

            if ((IsKeyPressed(KEY_ONE) || (hoverEasy && clicked)) && difficulty != DIFF_EASY) {
                difficulty = DIFF_EASY;
                if (sfxOn) PlaySound(menuSelectSound);
            }
            if ((IsKeyPressed(KEY_TWO) || (hoverMedium && clicked)) && difficulty != DIFF_MEDIUM) {
                difficulty = DIFF_MEDIUM;
                if (sfxOn) PlaySound(menuSelectSound);
            }
            if ((IsKeyPressed(KEY_THREE) || (hoverHard && clicked)) && difficulty != DIFF_HARD) {
                difficulty = DIFF_HARD;
                if (sfxOn) PlaySound(menuSelectSound);
            }

            // ---- START! button ----
            Rectangle startBtn = { screenWidth / 2 - 158, 670, 320, 66 };
            bool hoverStart = CheckCollisionPointRec(mouse, startBtn);
            bool pressedStart = IsKeyPressed(KEY_ENTER) || (hoverStart && clicked);

            if (pressedStart) {
                if (sfxOn) PlaySound(menuSelectSound);

                // ---- Reset run state for a fresh game (name + difficulty are kept as chosen) ----
                playerPosition = (Vector2){365, 2955};
                playerSpeed = Vector2Zero();
                prevPosition = playerPosition;
                camera.target = playerPosition;
                isGrounded = true;
                riding = -1;
                playerHealth = PLAYER_MAX_HEALTH;
                playerDefeated = false;
                playerDefeatTimer = 0;
                playerHurtCooldown = 0;
                playerDying = false;
                playerDeathBlinkTimer = 0;
                playerDeathBlinkCount = 0;
                playerVisible = true;
                gameOver = false;
                gameWon = false;
                scoreRecorded = false;
                gameOverTimer = 0.0f;
                paused = false;
                pauseMenuSelection = 0;
                for (int i = 0; i < LEVEL_COUNT; i++) levelFinished[i] = false;
                levelPopupToShow = -1;
                levelPopupTimer = 0;

                for (int i = 0; i < coinCount; i++) {
                    coins[i].active = true;
                    coins[i].collecting = false;
                    coins[i].frame = 0;
                    coins[i].timer = 0.0f;
                }
                for (int i = 0; i < mpCount; i++) {
                    mplat[i].pos = mplat[i].start;
                    mplat[i].t = 0;
                    mplat[i].dir = 1;
                    mplat[i].delta = Vector2Zero();
                }

                // Spawn enemies according to the chosen difficulty
                enemyCount = 0;
                float minWidth    = difficultySettings[difficulty].minPlatformWidth;
                int   perPlatform = difficultySettings[difficulty].enemiesPerPlatform;
                for (int i = 0; i < platformNum && enemyCount < MAX_ENEMIES; i++) {
                    if (platform[i].width < minWidth) continue;

                    float lo = platform[i].x + enemyEdgeMargin;
                    float hi = platform[i].x + platform[i].width - enemyEdgeMargin;
                    if (hi <= lo) continue;

                    for (int k = 0; k < perPlatform && enemyCount < MAX_ENEMIES; k++) {
                        float t = (k + 1.0f) / (perPlatform + 1.0f);   // spread evenly if more than one
                        Enemy *e = &enemies[enemyCount++];
                        e->patrolMinX = lo;
                        e->patrolMaxX = hi;
                        e->pos = (Vector2){ lo + (hi - lo) * t, platform[i].y };
                        e->facing = 1;
                        e->state = ENEMY_PATROL;
                        e->sprites = enemyWalk;
                        e->frameCount = 11;
                        e->frame = 0;
                        e->animTimer = 0;
                        e->attackTimer = ENEMY_ATTACK_INTERVAL;
                        e->fightFlag = false;
                        e->blinkTimer = 0;
                        e->blinkCount = 0;
                        e->visible = true;
                        e->alive = true;
                    }
                }

                // A fresh player also gets a fresh coin/score tally
                score = 0;
                coinsCollected = 0;

                screen = SCREEN_PLAYING;
            }

            BeginDrawing();
                DrawScreenBackground(startBg, screenWidth, screenHeight, 0.55f);
                DrawCenteredTextShadow("START GAME", (int)screenWidth / 2, 40, 40, GOLD);

                DrawCenteredText("ENTER YOUR NAME:", (int)screenWidth / 2, 100, 22, LIGHTGRAY);
                Rectangle nameBox = { screenWidth / 2 - 220, 128, 440, 50 };
                DrawRectangleRounded(nameBox, 0.25f, 8, Fade(BLACK, 0.5f));
                DrawRectangleRoundedLines(nameBox, 0.25f, 8, RAYWHITE);
                char nameDisplay[32];
                snprintf(nameDisplay, sizeof(nameDisplay), "%s_", playerNameBuf);
                DrawCenteredText(nameDisplay, (int)screenWidth / 2, (int)nameBox.y + 12, 26, GOLD);

                DrawCenteredText("SELECT DIFFICULTY", (int)screenWidth / 2, 260, 22, LIGHTGRAY);

                DrawRectangleRounded(easyBtn, 0.3f, 10, difficulty == DIFF_EASY ? (Color){60, 175, 85, 255} : Fade(BLACK, 0.5f));
                DrawRectangleRoundedLines(easyBtn, 0.3f, 10, difficulty == DIFF_EASY ? GOLD : RAYWHITE);
                DrawCenteredText("EASY", (int)(easyBtn.x + easyBtn.width / 2), (int)(easyBtn.y + 15), 24, RAYWHITE);

                DrawRectangleRounded(mediumBtn, 0.3f, 10, difficulty == DIFF_MEDIUM ? (Color){235, 165, 55, 255} : Fade(BLACK, 0.5f));
                DrawRectangleRoundedLines(mediumBtn, 0.3f, 10, difficulty == DIFF_MEDIUM ? GOLD : RAYWHITE);
                DrawCenteredText("MEDIUM", (int)(mediumBtn.x + mediumBtn.width / 2), (int)(mediumBtn.y + 15), 24, RAYWHITE);

                DrawRectangleRounded(hardBtn, 0.3f, 10, difficulty == DIFF_HARD ? (Color){220, 65, 65, 255} : Fade(BLACK, 0.5f));
                DrawRectangleRoundedLines(hardBtn, 0.3f, 10, difficulty == DIFF_HARD ? GOLD : RAYWHITE);
                DrawCenteredText("HARD", (int)(hardBtn.x + hardBtn.width / 2), (int)(hardBtn.y + 15), 24, RAYWHITE);

                // Subtle pulsing highlight on the START button so it reads as the primary action
                float startPulse = 1.0f + 0.03f * sinf((float)GetTime() * 5.0f);
                Rectangle startBtnPulsed = {
                    startBtn.x - startBtn.width * (startPulse - 1.0f) / 2.0f,
                    startBtn.y - startBtn.height * (startPulse - 1.0f) / 2.0f,
                    startBtn.width * startPulse, startBtn.height * startPulse
                };
                DrawRectangleRounded(startBtnPulsed, 0.3f, 10, hoverStart ? (Color){235, 165, 55, 255} : (Color){215, 140, 30, 255});
                DrawRectangleRoundedLines(startBtnPulsed, 0.3f, 10, GOLD);
                DrawCenteredText("START!", (int)(startBtn.x + startBtn.width / 2), (int)(startBtn.y + 18), 32, BLACK);

                DrawCenteredTextShadow("TOP 5 SCORES", (int)screenWidth / 2, 400, 30, GOLD);
                if (highScoreCount == 0) {
                    DrawCenteredText("No scores yet -- be the first!", (int)screenWidth / 2, 450, 20, LIGHTGRAY);
                } else {
                    for (int i = 0; i < highScoreCount; i++) {
                        char row[64];
                        snprintf(row, sizeof(row), "%d. %-12s %d", i + 1, highScores[i].name, highScores[i].score);
                        DrawCenteredText(row, (int)screenWidth / 2, 450 + i * 34, 24, RAYWHITE);
                    }
                }

                DrawRectangleRounded(backBtn, 0.3f, 8, hoverBack ? (Color){90, 90, 110, 255} : (Color){60, 60, 75, 255});
                DrawRectangleRoundedLines(backBtn, 0.3f, 8, RAYWHITE);
                DrawCenteredText("< BACK", (int)(backBtn.x + backBtn.width / 2), (int)(backBtn.y + 12), 20, RAYWHITE);
            EndDrawing();
            continue;
        }

        // ==================================================================
        // SCREEN: Playing -- everything below only runs once screen ==
        // SCREEN_PLAYING (every other screen above `continue`d before here).
        // ==================================================================

        // ---- Pause button + pause menu ----
        Rectangle pauseBtn = { screenWidth / 2 - 25, 14, 50, 40 };
        bool hoverPauseBtn = CheckCollisionPointRec(GetMousePosition(), pauseBtn);

        // ESC/P toggles pause; only while a run is actually in progress (not during
        // the Game Over / Game Complete overlays, where it wouldn't make sense).
        if (!gameOver && !gameWon) {
            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE) || (hoverPauseBtn && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))) {
                paused = !paused;
                if (sfxOn) PlaySound(menuSelectSound);
            }
        }

        // Pause menu layout is computed every frame (cheap) so both the input
        // handling below and the drawing code further down can reuse it.
        const char *pauseItems[2] = { "RESUME", "MAIN MENU" };
        Rectangle pauseItemRects[2];
        float pauseItemW = 300, pauseItemH = 56, pauseItemGap = 16;
        float pauseStartY = screenHeight / 2 - 20;
        for (int i = 0; i < 2; i++) {
            pauseItemRects[i] = (Rectangle){ screenWidth / 2 - pauseItemW / 2, pauseStartY + i * (pauseItemH + pauseItemGap), pauseItemW, pauseItemH };
        }

        if (paused) {
            Vector2 pmouse = GetMousePosition();
            int prevPauseSelection = pauseMenuSelection;
            for (int i = 0; i < 2; i++) {
                if (CheckCollisionPointRec(pmouse, pauseItemRects[i])) pauseMenuSelection = i;
            }
            if (pauseMenuSelection != prevPauseSelection && sfxOn) PlaySound(menuSelectSound);

            if (IsKeyPressed(KEY_DOWN)) { pauseMenuSelection = (pauseMenuSelection + 1) % 2; if (sfxOn) PlaySound(menuSelectSound); }
            if (IsKeyPressed(KEY_UP))   { pauseMenuSelection = (pauseMenuSelection - 1 + 2) % 2; if (sfxOn) PlaySound(menuSelectSound); }

            bool pauseActivate = IsKeyPressed(KEY_ENTER) ||
                                  (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(pmouse, pauseItemRects[pauseMenuSelection]));
            if (pauseActivate) {
                if (sfxOn) PlaySound(menuSelectSound);
                if (pauseMenuSelection == 0) {
                    paused = false;               // RESUME
                } else {
                    paused = false;               // MAIN MENU
                    screen = SCREEN_START_GAME;
                }
            }
        }

        // The whole simulation freezes once the run has ended (death or victory) or
        // the game is paused; only overlays + the relevant menu logic still run.
        if (!gameOver && !gameWon && !paused) {

            // ---- Moving platforms ----
            for (int i = 0; i < mpCount; i++) {
                MovingPlatform *m = &mplat[i];
                float len = Vector2Distance(m->start, m->end);
                m->t += m->dir * (m->speed * dt) / len;
                if (m->t >= 1.0f) { m->t = 1.0f; m->dir = -1; }
                if (m->t <= 0.0f) { m->t = 0.0f; m->dir = 1; }
                Vector2 newPos = Vector2Lerp(m->start, m->end, m->t);
                m->delta = Vector2Subtract(newPos, m->pos);
                m->pos = newPos;
            }
            // Carry the player along with whichever platform they're standing on
            if (riding >= 0) playerPosition = Vector2Add(playerPosition, mplat[riding].delta);

            // ---- Gravity + horizontal/vertical movement ----
            playerSpeed = Vector2Add(playerSpeed, Vector2Scale(gravity, dt));
            playerPosition = Vector2Add(playerPosition, Vector2Scale(playerSpeed, dt));

            Rectangle playerRect = {
                playerPosition.x,
                playerPosition.y - sprites[spriteIndex].height,
                sprites[spriteIndex].width,
                sprites[spriteIndex].height
            };

            // ---- Collision against static platforms ----
            isGrounded = false;
            riding = -1;
            for (int i = 0; i < platformNum; i++) {
                if (!CheckCollisionRecs(playerRect, platform[i])) continue;

                Rectangle p = platform[i];
                bool wasAbove  = prevPosition.y <= p.y + 1.0f;   // feet were above the platform last frame
                bool overlapsX = playerRect.x + playerRect.width > p.x && playerRect.x < p.x + p.width;

                if (playerSpeed.y >= 0 && wasAbove && overlapsX) {
                    // Landing on top
                    playerPosition.y = p.y;
                    playerSpeed.y = 0;
                    isGrounded = true;
                    playerRect.y = playerPosition.y - playerRect.height;
                } else {
                    // Side collision -- push out toward whichever edge is closer
                    float overlapLeft  = (playerRect.x + playerRect.width) - p.x;
                    float overlapRight = (p.x + p.width) - playerRect.x;
                    if (overlapLeft < overlapRight) playerPosition.x -= overlapLeft;
                    else                            playerPosition.x += overlapRight;
                    playerRect.x = playerPosition.x;
                }
            }

            // ---- Collision against moving platforms (one-way: only land from above) ----
            float footL = playerPosition.x + 80;   // matches the narrower hitbox used below
            float footR = footL + 80;
            for (int i = 0; i < mpCount; i++) {
                MovingPlatform *m = &mplat[i];
                float oldTop     = m->pos.y - m->delta.y;   // where the top was last frame
                bool wasAbove    = prevPosition.y <= oldTop + 1.0f;
                bool overlapsX   = footR > m->pos.x && footL < m->pos.x + MP_W;
                bool reachedTop  = playerPosition.y >= m->pos.y;

                if (playerSpeed.y >= 0 && wasAbove && overlapsX && reachedTop) {
                    playerPosition.y = m->pos.y;
                    playerSpeed.y = 0;
                    isGrounded = true;
                    riding = i;
                }
            }

            prevPosition = playerPosition; // remember for next frame's landing checks

            // Don't let the player cross the world's left edge (x = 0)
            if (playerPosition.x < 0.0f) {
                playerPosition.x = 0.0f;
                if (playerSpeed.x < 0.0f) playerSpeed.x = 0.0f;
                prevPosition.x = playerPosition.x;
            }
            // ...or jump above the world's top edge (y = 0)
            if (playerPosition.y < 0.0f) {
                playerPosition.y = 0.0f;
                if (playerSpeed.y < 0.0f) playerSpeed.y = 0.0f;
                prevPosition.y = playerPosition.y;
            }

            // ---- Player input (movement / jump / fight) ----
            if (!playerDefeated && !playerDying) {
                if (IsKeyDown(KEY_RIGHT)) {
                    playerSpeed.x = playerSpeedMax;
                    sprites = playerRun;
                    flip = false;
                } else if (IsKeyDown(KEY_LEFT)) {
                    playerSpeed.x = -playerSpeedMax;
                    sprites = playerRun;
                    flip = true;
                } else {
                    playerSpeed.x = 0;
                    sprites = playerIdle;
                }

                if (IsKeyPressed(KEY_SPACE) && isGrounded) {
                    playerSpeed.y = -playerSpeedUp;
                    sprites = playerJump;
                    isGrounded = false;
                    riding = -1;
                    if (sfxOn) PlaySound(jumpSound);   // plays immediately on takeoff
                }

                if (!isGrounded) sprites = playerJump;

                if (IsKeyDown(KEY_X)) {
                    srand(time(NULL));
                    sprites = playerFight[rand() % 3];
                }
            } else {
                playerSpeed.x = 0; // no directional control while reeling from a hit or dying
            }

            // Highest-priority sprite overrides
            if (playerDying) sprites = playerDead;
            else if (playerDefeated) sprites = playerDefeat;

            // ---- Coin collecting ----
            Rectangle playerHitbox = { playerPosition.x + 80, playerPosition.y - 170, 80, 170 };

            for (int i = 0; i < coinCount; i++) {
                if (!coins[i].active) continue;

                coins[i].timer += dt;
                if (coins[i].timer >= COIN_FRAME_TIME) {
                    coins[i].timer -= COIN_FRAME_TIME;
                    coins[i].frame++;
                    if (coins[i].collecting) {
                        if (coins[i].frame >= 6) {   // collect animation finished
                            coins[i].active = false;
                            continue;
                        }
                    } else {
                        coins[i].frame %= 7;   // spin animation loops forever
                    }
                }

                if (!coins[i].collecting) {
                    Rectangle coinRect = { coins[i].pos.x, coins[i].pos.y, COIN_SIZE, COIN_SIZE };
                    if (CheckCollisionRecs(playerHitbox, coinRect)) {
                        coins[i].collecting = true;
                        coins[i].frame = 0;
                        coins[i].timer = 0.0f;
                        score += 10;
                        coinsCollected++;
                        if (sfxOn) PlaySound(coinSound);
                    }
                }
            }

            // ---- Enemy AI ----
            for (int i = 0; i < enemyCount; i++) {
                Enemy *e = &enemies[i];
                if (!e->alive) continue;

                // Already dying: just run the disappear-blink and skip all other logic
                if (e->state == ENEMY_DYING) {
                    e->blinkTimer += dt;
                    if (e->blinkTimer >= BLINK_TIME) {
                        e->blinkTimer -= BLINK_TIME;
                        e->visible = !e->visible;
                        e->blinkCount++;
                        if (e->blinkCount >= BLINK_COUNT) e->alive = false;
                    }
                    continue;
                }

                float dx = playerPosition.x - e->pos.x;
                float absDx = fabsf(dx);
                bool sameLevel    = fabsf(playerPosition.y - e->pos.y) < 80.0f;
                bool facingPlayer = (dx > 0 && e->facing == 1) || (dx < 0 && e->facing == -1);
                bool backExposed  = !facingPlayer;

                // Backstab: pressing X while the enemy's back is turned to the player
                // (and close enough) kills it instantly, regardless of its current state.
                if (backExposed && sameLevel && absDx < ENEMY_ATTACK_RANGE && IsKeyPressed(KEY_X)) {
                    e->state = ENEMY_DYING;
                    e->blinkTimer = 0;
                    e->blinkCount = 0;
                    e->visible = true;
                    score += ENEMY_KILL_POINTS;
                    if (sfxOn) PlaySound(enemyDefeatSound);
                    continue;
                }

                switch (e->state) {
                    case ENEMY_PATROL: {
                        e->pos.x += e->facing * ENEMY_PATROL_SPEED * dt;
                        if (e->pos.x >= e->patrolMaxX) { e->pos.x = e->patrolMaxX; e->facing = -1; }
                        if (e->pos.x <= e->patrolMinX) { e->pos.x = e->patrolMinX; e->facing = 1; }

                        if (sameLevel && absDx < ENEMY_DETECT_RANGE && facingPlayer) {
                            e->state = ENEMY_CHASE;
                            e->sprites = enemyRun;
                            e->frameCount = 7;
                            e->frame = 0;
                        }
                        break;
                    }
                    case ENEMY_CHASE: {
                        e->facing = (dx > 0) ? 1 : -1;
                        e->pos.x += e->facing * ENEMY_RUN_SPEED * dt;

                        if (absDx < ENEMY_ATTACK_RANGE && sameLevel) {
                            e->state = ENEMY_ATTACK;
                            e->sprites = enemyAttack;
                            e->frameCount = 14;
                            e->frame = 0;
                            e->attackTimer = ENEMY_ATTACK_INTERVAL;
                            e->fightFlag = false;
                        } else if (absDx > ENEMY_LOSE_RANGE || !sameLevel) {
                            e->state = ENEMY_PATROL;
                            e->sprites = enemyWalk;
                            e->frameCount = 11;
                        }
                        break;
                    }
                    case ENEMY_ATTACK: {
                        e->facing = (dx > 0) ? 1 : -1;

                        if (absDx > ENEMY_ATTACK_RANGE * 1.5f) {
                            e->state = ENEMY_CHASE;
                            e->sprites = enemyRun;
                            e->frameCount = 7;
                            e->fightFlag = false;
                            break;
                        }

                        // Any X press during the whole attack window counts as fighting
                        // back -- not just the single frame the timer happens to tick on.
                        if (IsKeyDown(KEY_X)) e->fightFlag = true;

                        e->attackTimer -= dt;
                        if (e->attackTimer <= 0.0f) {
                            e->attackTimer = ENEMY_ATTACK_INTERVAL;

                            if (e->fightFlag) {
                                // Player fought back -- this enemy dies
                                e->state = ENEMY_DYING;
                                e->blinkTimer = 0;
                                e->blinkCount = 0;
                                e->visible = true;
                                score += ENEMY_KILL_POINTS;
                                if (sfxOn) PlaySound(enemyDefeatSound);
                            } else if (playerHurtCooldown <= 0.0f) {
                                playerHealth--;
                                playerDefeated = true;
                                playerDefeatTimer = PLAYER_DEFEAT_TIME;
                                playerHurtCooldown = PLAYER_HURT_COOLDOWN;
                                if (sfxOn) PlaySound(hurtSound);

                                // Player just ran out of health: the game-over sound
                                // plays right now, at the moment of defeat -- not after
                                // the multi-second death-blink sequence finishes below.
                                if (playerHealth <= 0 && !playerDying) {
                                    playerDying = true;
                                    if (sfxOn) PlaySound(gameOverSound);
                                }
                            }
                            e->fightFlag = false;
                        }
                        break;
                    }
                    default: break;
                }

                // Animation advance (skipped for DYING, which uses the blink instead)
                e->animTimer += dt;
                if (e->animTimer >= ENEMY_ANIM_FRAME_TIME) {
                    e->animTimer -= ENEMY_ANIM_FRAME_TIME;
                    e->frame = (e->frame + 1) % e->frameCount;
                }
            }

            if (playerHurtCooldown > 0.0f) playerHurtCooldown -= dt;
            if (playerDefeatTimer > 0.0f) {
                playerDefeatTimer -= dt;
                if (playerDefeatTimer <= 0.0f) playerDefeated = false;
            }

            // ---- Level progression: did the player cross into a new level? ----
            for (int i = 0; i < LEVEL_COUNT; i++) {
                if (!levelFinished[i] && playerPosition.x >= levelBoundaries[i + 1]) {
                    levelFinished[i] = true;

                    if (i == LEVEL_COUNT - 1) {
                        // Finished the final level: the game is won
                        gameWon = true;
                        gameOverTimer = GAME_OVER_DELAY;
                        if (sfxOn) PlaySound(victorySound);
                        if (!scoreRecorded) {
                            const char *finalName = (playerNameLen > 0) ? playerNameBuf : "PLAYER";
                            InsertHighScore(highScores, &highScoreCount, finalName, score);
                            SaveHighScores(highScores, highScoreCount);
                            scoreRecorded = true;
                        }
                    } else {
                        levelPopupToShow = i;
                        levelPopupTimer = LEVEL_POPUP_DURATION;
                        if (sfxOn) PlaySound(levelCompleteSound);
                    }
                }
            }
            if (levelPopupTimer > 0.0f) levelPopupTimer -= dt;

        } // end if (!gameOver && !gameWon && !paused)

        // ---- Death-blink sequence: plays out even though the block above is frozen ----
        if (playerDying && !gameOver) {
            playerDeathBlinkTimer += dt;
            if (playerDeathBlinkTimer >= BLINK_TIME) {
                playerDeathBlinkTimer -= BLINK_TIME;
                playerVisible = !playerVisible;
                playerDeathBlinkCount++;
                if (playerDeathBlinkCount >= BLINK_COUNT * 2) {
                    gameOver = true;
                    playerVisible = false;
                    gameOverTimer = GAME_OVER_DELAY;
                    if (!scoreRecorded) {
                        const char *finalName = (playerNameLen > 0) ? playerNameBuf : "PLAYER";
                        InsertHighScore(highScores, &highScoreCount, finalName, score);
                        SaveHighScores(highScores, highScoreCount);
                        scoreRecorded = true;
                    }
                }
            }
        }

        // Once the run has ended (death or victory), the score is already saved (the
        // player typed their name back on the Start Game screen) -- just count down and
        // return there so the updated leaderboard is visible. ENTER skips the wait.
        if ((gameOver || gameWon) && screen == SCREEN_PLAYING) {
            gameOverTimer -= dt;
            if (gameOverTimer <= 0.0f || IsKeyPressed(KEY_ENTER)) {
                screen = SCREEN_START_GAME;
            }
        }

        // Smoothly ease the camera toward the player instead of snapping to them
        camera.target = Vector2Lerp(camera.target, playerPosition, 1.0f - expf(-12.0f * dt));
        // Don't let the camera scroll past the world's edges (x = 0 or y = 0), or it
        // would show empty space outside the map.
        if (camera.target.x < camera.offset.x) camera.target.x = camera.offset.x;
        if (camera.target.y < camera.offset.y) camera.target.y = camera.offset.y;

        // ------------------------------------------------------------
        // Drawing
        // ------------------------------------------------------------
        BeginDrawing();
        ClearBackground(RAYWHITE);
            BeginMode2D(camera);
                DrawTextureEx(bg, (Vector2){0, 0}, 0.0f, 5.0f, WHITE);
                DrawTextureEx(map, (Vector2){0, 0}, 0.0f, 5.0f, WHITE);

                for (int i = 0; i < coinCount; i++) {
                    if (!coins[i].active) continue;
                    Texture2D tex = coins[i].collecting ? coinCollect[coins[i].frame] : coinSpin[coins[i].frame];
                    DrawTexturePro(tex, (Rectangle){0, 0, tex.width, tex.height},
                        (Rectangle){coins[i].pos.x, coins[i].pos.y, COIN_SIZE, COIN_SIZE}, Vector2Zero(), 0, WHITE);
                }

                for (int i = 0; i < mpCount; i++) {
                    DrawTextureV(movingPlatform, mplat[i].pos, WHITE);
                }

                for (int i = 0; i < enemyCount; i++) {
                    Enemy *e = &enemies[i];
                    if (!e->alive) continue;
                    if (e->state == ENEMY_DYING && !e->visible) continue;

                    Texture2D t = e->sprites[e->frame];
                    Rectangle src = e->facing < 0 ? (Rectangle){0, 0, -t.width, t.height} : (Rectangle){0, 0, t.width, t.height};
                    Rectangle dst = { e->pos.x, e->pos.y - t.height, t.width, t.height };
                    DrawTexturePro(t, src, dst, Vector2Zero(), 0, WHITE);
                }

                if (playerVisible) {
                    Rectangle src = flip
                        ? (Rectangle){0, 0, -sprites[spriteIndex].width, sprites[spriteIndex].height}
                        : (Rectangle){0, 0, sprites[spriteIndex].width, sprites[spriteIndex].height};
                    Rectangle dst = { playerPosition.x, playerPosition.y - sprites[spriteIndex].height,
                                       sprites[spriteIndex].width, sprites[spriteIndex].height };
                    DrawTexturePro(sprites[spriteIndex], src, dst, Vector2Zero(), 0, WHITE);
                }

                DrawTextureEx(bush, (Vector2){0, 0}, 0.0f, 5.0f, WHITE);
            EndMode2D();

            // ---- HUD panel: score, coins, lives, player name, difficulty ----
            DrawRectangleRounded((Rectangle){10, 10, 300, 232}, 0.12f, 8, Fade(BLACK, 0.35f));
            DrawText(TextFormat("Score: %d", score), 24, 20, 44, GOLD);
            DrawText(TextFormat("Coins: %d", coinsCollected), 24, 72, 26, RAYWHITE);

            // Life boxes: 3 boxes, one per life; each filled while that life remains
            {
                int lifeBoxSize = 36, lifeBoxGap = 10, lifeBoxX = 24, lifeBoxY = 108;
                for (int i = 0; i < PLAYER_MAX_HEALTH; i++) {
                    Rectangle box = { lifeBoxX + i * (lifeBoxSize + lifeBoxGap), lifeBoxY, lifeBoxSize, lifeBoxSize };
                    DrawRectangleRounded(box, 0.25f, 6, (Color){40, 40, 40, 255});
                    DrawRectangleRoundedLines(box, 0.25f, 6, RAYWHITE);
                    if (i < playerHealth) {
                        Rectangle fill = { box.x + 5, box.y + 5, box.width - 10, box.height - 10 };
                        DrawRectangleRounded(fill, 0.25f, 6, RED);
                    }
                }
            }
            DrawText(TextFormat("Player: %s", (playerNameLen > 0) ? playerNameBuf : "PLAYER"), 24, 160, 20, LIGHTGRAY);
            DrawText(TextFormat("Difficulty: %s", difficultyNames[difficulty]), 24, 186, 20, LIGHTGRAY);

            // ---- Level badge (top-right): which of the 3 map segments the player is in ----
            {
                int displayLevel = 0;
                for (int i = 0; i < LEVEL_COUNT; i++) {
                    if (playerPosition.x >= levelBoundaries[i]) displayLevel = i;
                }
                char levelBuf[32];
                snprintf(levelBuf, sizeof(levelBuf), "LEVEL %d / %d", displayLevel + 1, LEVEL_COUNT);
                int lw = MeasureText(levelBuf, 28);
                Rectangle levelBadge = { screenWidth - lw - 56, 20, (float)(lw + 32), 44 };
                DrawRectangleRounded(levelBadge, 0.3f, 8, Fade(BLACK, 0.55f));
                DrawRectangleRoundedLines(levelBadge, 0.3f, 8, GOLD);
                DrawText(levelBuf, (int)levelBadge.x + 16, (int)levelBadge.y + 8, 28, GOLD);
            }

            // ---- Level complete popup: fades in, holds, fades out ----
            if (levelPopupTimer > 0.0f && levelPopupToShow >= 0) {
                float elapsed = LEVEL_POPUP_DURATION - levelPopupTimer;
                float alpha = 1.0f;
                if (elapsed < LEVEL_POPUP_FADE) alpha = elapsed / LEVEL_POPUP_FADE;
                else if (levelPopupTimer < LEVEL_POPUP_FADE) alpha = levelPopupTimer / LEVEL_POPUP_FADE;
                if (alpha < 0.0f) alpha = 0.0f;
                if (alpha > 1.0f) alpha = 1.0f;

                Rectangle panel = { screenWidth / 2 - 320, 90, 640, 130 };
                DrawRectangleRounded(panel, 0.15f, 10, Fade(BLACK, 0.75f * alpha));
                DrawRectangleRoundedLines(panel, 0.15f, 10, Fade(GOLD, alpha));

                char titleBuf[48];
                snprintf(titleBuf, sizeof(titleBuf), "LEVEL %d COMPLETE!", levelPopupToShow + 1);
                DrawCenteredText(titleBuf, (int)screenWidth / 2, (int)panel.y + 28, 42, Fade(GOLD, alpha));

                char subBuf[48];
                snprintf(subBuf, sizeof(subBuf), "Entering Level %d", levelPopupToShow + 2);
                DrawCenteredText(subBuf, (int)screenWidth / 2, (int)panel.y + 82, 24, Fade(RAYWHITE, alpha));
            }

            // ---- Game Over overlay ----
            if (gameOver) {
                DrawRectangle(0, 0, (int)screenWidth, (int)screenHeight, Fade(BLACK, 0.55f));
                DrawCenteredTextShadow("GAME OVER", (int)screenWidth / 2, (int)screenHeight / 2 - 100, 64, RED);
                DrawCenteredText(TextFormat("Final Score: %d", score), (int)screenWidth / 2, (int)screenHeight / 2 - 20, 30, RAYWHITE);
                DrawCenteredText("Score saved to the leaderboard", (int)screenWidth / 2, (int)screenHeight / 2 + 20, 22, GREEN);
                DrawCenteredText(TextFormat("Returning to Start Menu in %.0fs (or press ENTER)", gameOverTimer > 0 ? gameOverTimer : 0),
                                 (int)screenWidth / 2, (int)screenHeight / 2 + 60, 20, GRAY);
            }

            // ---- Game Complete overlay ----
            if (gameWon) {
                DrawRectangle(0, 0, (int)screenWidth, (int)screenHeight, Fade(BLACK, 0.6f));
                DrawCenteredTextShadow("GAME COMPLETE!", (int)screenWidth / 2, (int)screenHeight / 2 - 110, 60, GOLD);
                DrawCenteredText("You defeated all three levels!", (int)screenWidth / 2, (int)screenHeight / 2 - 40, 26, RAYWHITE);
                DrawCenteredText(TextFormat("Final Score: %d", score), (int)screenWidth / 2, (int)screenHeight / 2, 30, RAYWHITE);
                DrawCenteredText("Score saved to the leaderboard", (int)screenWidth / 2, (int)screenHeight / 2 + 40, 22, GREEN);
                DrawCenteredText(TextFormat("Returning to Start Menu in %.0fs (or press ENTER)", gameOverTimer > 0 ? gameOverTimer : 0),
                                 (int)screenWidth / 2, (int)screenHeight / 2 + 80, 20, GRAY);
            }

            // ---- Pause button (visible whenever the run is still active) ----
            if (!gameOver && !gameWon) {
                DrawRectangleRounded(pauseBtn, 0.25f, 8, hoverPauseBtn ? (Color){90, 90, 110, 255} : Fade(BLACK, 0.5f));
                DrawRectangleRoundedLines(pauseBtn, 0.25f, 8, RAYWHITE);
                DrawCenteredText(paused ? ">" : "II", (int)(pauseBtn.x + pauseBtn.width / 2), (int)(pauseBtn.y + 8), 24, RAYWHITE);
            }

            // ---- Pause menu overlay ----
            if (paused) {
                DrawRectangle(0, 0, (int)screenWidth, (int)screenHeight, Fade(BLACK, 0.6f));
                DrawCenteredTextShadow("PAUSED", (int)screenWidth / 2, (int)pauseStartY - 90, 56, GOLD);
                for (int i = 0; i < 2; i++) {
                    bool sel = (i == pauseMenuSelection);
                    DrawRectangleRounded(pauseItemRects[i], 0.3f, 10, sel ? (Color){235, 165, 55, 255} : Fade(BLACK, 0.5f));
                    DrawRectangleRoundedLines(pauseItemRects[i], 0.3f, 10, sel ? GOLD : RAYWHITE);
                    DrawCenteredText(pauseItems[i], (int)(pauseItemRects[i].x + pauseItemRects[i].width / 2),
                                     (int)(pauseItemRects[i].y + 16), 26, sel ? BLACK : RAYWHITE);
                }
            }
        EndDrawing();

        // ------------------------------------------------------------
        // Player animation: figure out how many frames the current
        // animation set has, then advance through them at a steady
        // rate -- restarting at frame 0 whenever the set changes so a
        // new animation (e.g. idle -> jump) never starts mid-cycle.
        // ------------------------------------------------------------
        int spriteNum = 8; // safe default
        if (sprites == playerIdle)        spriteNum = 8;
        else if (sprites == playerRun)    spriteNum = 10;
        else if (sprites == playerJump)   spriteNum = 7;
        else if (sprites == playerDefeat) spriteNum = 10;
        else if (sprites == playerDead)   spriteNum = 9;
        else {
            for (int i = 0; i < 3; i++) {
                if (sprites == playerFight[i]) { spriteNum = 8; break; }
            }
        }

        if (sprites != prevAnimSprites) {
            spriteIndex = 0;
            playerAnimTimer = 0.0f;
            prevAnimSprites = sprites;
        }
        playerAnimTimer += dt;
        if (playerAnimTimer >= 0.07f) {
            playerAnimTimer -= 0.07f;
            spriteIndex = (spriteIndex + 1) % spriteNum;
        }
    }

    // =========================================================================
    // Cleanup
    // =========================================================================
    for (int i = 0; i < 8; i++)  UnloadTexture(playerIdle[i]);
    for (int i = 0; i < 10; i++) UnloadTexture(playerRun[i]);
    for (int i = 0; i < 7; i++)  UnloadTexture(playerJump[i]);
    for (int i = 0; i < 10; i++) UnloadTexture(playerDefeat[i]);
    for (int i = 0; i < 9; i++)  UnloadTexture(playerDead[i]);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 8; j++) UnloadTexture(playerFight[i][j]);

    for (int i = 0; i < 11; i++) UnloadTexture(enemyWalk[i]);
    for (int i = 0; i < 7; i++)  UnloadTexture(enemyRun[i]);
    for (int i = 0; i < 14; i++) UnloadTexture(enemyAttack[i]);

    for (int i = 0; i < 7; i++) UnloadTexture(coinSpin[i]);
    for (int i = 0; i < 6; i++) UnloadTexture(coinCollect[i]);

    UnloadTexture(bg);
    UnloadTexture(map);
    UnloadTexture(bush);
    UnloadTexture(movingPlatform);
    UnloadTexture(menuBg);
    UnloadTexture(startBg);

    UnloadMusicStream(musicbg);
    UnloadSound(jumpSound);
    UnloadSound(menuSelectSound);
    UnloadSound(menuToggleSound);
    UnloadSound(coinSound);
    UnloadSound(hurtSound);
    UnloadSound(enemyDefeatSound);
    UnloadSound(levelCompleteSound);
    UnloadSound(victorySound);
    UnloadSound(gameOverSound);
    CloseAudioDevice();

    CloseWindow();
    return 0;
}