#include "raylib.h"

#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#ifndef ASSET_DIR_PATH
#define ASSET_DIR_PATH "assets"
#endif

typedef enum SpriteKind
{
    SPRITE_ROCK = 0,
    SPRITE_PAPER = 1,
    SPRITE_SCISSOR = 2
} SpriteKind;

typedef struct SpriteInstance
{
    SpriteKind kind;
    Vector2 position;
} SpriteInstance;

static bool resolve_asset_path(const char *file_name, char *resolved_path, size_t resolved_path_size)
{
    const char *asset_dirs[] = {
        ASSET_DIR_PATH,
        "assets",
        "../assets",
        "../../assets"};

    for (int i = 0; i < (int)(sizeof(asset_dirs) / sizeof(asset_dirs[0])); ++i)
    {
        snprintf(resolved_path, resolved_path_size, "%s/%s", asset_dirs[i], file_name);
        if (FileExists(resolved_path))
        {
            return true;
        }
    }

    return false;
}

static Texture2D load_texture_with_fallback(const char *path, Color fallback_color, bool *loaded_from_file)
{
    Texture2D texture = LoadTexture(path);
    if (texture.id != 0)
    {
        *loaded_from_file = true;
        return texture;
    }

    TraceLog(LOG_WARNING, "Using fallback texture for: %s", path);
    Image fallback = GenImageColor(64, 64, fallback_color);
    texture = LoadTextureFromImage(fallback);
    UnloadImage(fallback);
    *loaded_from_file = false;

    return texture;
}

int main(void)
{
    const int screen_width = 1280;
    const int screen_height = 960;
    const int sprite_draw_size = 64;
    const int sprites_per_kind = 10;
    const int total_sprites = sprites_per_kind * 3;

    InitWindow(screen_width, screen_height, "RPS Client Prototype");
    SetTargetFPS(60);

    char rock_path[512];
    char paper_path[512];
    char scissor_path[512];
    bool has_rock = resolve_asset_path("rock.png", rock_path, sizeof(rock_path));
    bool has_paper = resolve_asset_path("paper.png", paper_path, sizeof(paper_path));
    bool has_scissor = resolve_asset_path("scissor.png", scissor_path, sizeof(scissor_path));

    if (!has_rock || !has_paper || !has_scissor)
    {
        TraceLog(LOG_ERROR, "Missing one or more required textures (rock.png, paper.png, scissor.png)");

        while (!WindowShouldClose())
        {
            BeginDrawing();
            ClearBackground(RAYWHITE);
            DrawText("Texture load failed.", 40, 60, 32, MAROON);
            DrawText("Expected files:", 40, 120, 24, DARKGRAY);
            DrawText("assets/rock.png", 60, 160, 22, DARKGRAY);
            DrawText("assets/paper.png", 60, 190, 22, DARKGRAY);
            DrawText("assets/scissor.png", 60, 220, 22, DARKGRAY);
            DrawText("Press ESC or close window.", 40, 280, 22, GRAY);
            EndDrawing();
        }

        CloseWindow();
        return 1;
    }

    bool rock_from_file = false;
    bool paper_from_file = false;
    bool scissor_from_file = false;
    Texture2D rock = load_texture_with_fallback(rock_path, GRAY, &rock_from_file);
    Texture2D paper = load_texture_with_fallback(paper_path, LIGHTGRAY, &paper_from_file);
    Texture2D scissor = load_texture_with_fallback(scissor_path, DARKGRAY, &scissor_from_file);

    bool used_fallback = (!rock_from_file || !paper_from_file || !scissor_from_file);

    SpriteInstance sprites[30];
    SetRandomSeed((unsigned int)time(NULL));

    for (int i = 0; i < total_sprites; ++i)
    {
        SpriteKind kind = (SpriteKind)(i / sprites_per_kind);
        int x = GetRandomValue(0, screen_width - sprite_draw_size);
        int y = GetRandomValue(0, screen_height - sprite_draw_size);

        sprites[i].kind = kind;
        sprites[i].position = (Vector2){(float)x, (float)y};
    }

    while (!WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(RAYWHITE);

        for (int i = 0; i < total_sprites; ++i)
        {
            Texture2D texture = rock;
            if (sprites[i].kind == SPRITE_PAPER)
            {
                texture = paper;
            }
            else if (sprites[i].kind == SPRITE_SCISSOR)
            {
                texture = scissor;
            }

            Rectangle src = {0.0f, 0.0f, (float)texture.width, (float)texture.height};
            Rectangle dst = {sprites[i].position.x, sprites[i].position.y, (float)sprite_draw_size, (float)sprite_draw_size};
            DrawTexturePro(texture, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
        }

        DrawText("RPS Sprite Prototype", 20, 20, 24, DARKGRAY);
        if (used_fallback)
        {
            DrawText("Some sprites failed to decode; using placeholders.", 20, 52, 20, MAROON);
        }
        EndDrawing();
    }

    UnloadTexture(rock);
    UnloadTexture(paper);
    UnloadTexture(scissor);
    CloseWindow();

    return 0;
}
