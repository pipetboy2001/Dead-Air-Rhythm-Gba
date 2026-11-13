/*
 * Rhythm Caverns — juego de ritmo para GBA. El reloj es el contador de
 * frames, calibrado al tempo de cada canción vía FRAMES_PER_BEAT.
 */

#include "bn_core.h"
#include "bn_keypad.h"
#include "bn_colors.h"
#include "bn_fixed.h"
#include "bn_fixed_point.h"
#include "bn_optional.h"
#include "bn_utility.h"
#include "bn_vector.h"
#include "bn_string.h"
#include "bn_sprite_ptr.h"
#include "bn_sprite_palette_ptr.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_text_generator.h"
#include "bn_music.h"
#include "bn_bg_palettes.h"
#include "bn_music_item.h"
#include "bn_sram.h"
#include "bn_sound_items.h"

#include "bn_music_items.h"
#include "bn_regular_bg_items_arena.h"
#include "bn_regular_bg_items_menu_panel.h"
#include "bn_regular_bg_items_logo.h"
#include "bn_sprite_items_hero.h"
#include "bn_sprite_items_weapon_sword.h"
#include "bn_sprite_items_heart.h"
#include "bn_sprite_items_enemy_bat.h"
#include "bn_sprite_items_enemy_rat.h"
#include "bn_sprite_items_enemy_skeleton.h"
#include "bn_sprite_items_enemy_slime.h"
#include "bn_sprite_items_enemy_fly.h"
#include "bn_sprite_items_enemy_flying_skull.h"
#include "bn_sprite_items_enemy_ghost.h"
#include "bn_sprite_items_enemy_goblin.h"
#include "bn_sprite_items_enemy_spider.h"
#include "bn_sprite_items_hero_elf.h"
#include "bn_sprite_items_heart_pickup.h"

#include "common_variable_8x16_sprite_font.h"

#include "chart.h"

namespace
{
    // shortcut: calibrado desde el tempo de battle_clean.it (125 BPM) a frames
    // de GBA (~59.73/s): 60/125*59.7275 ~= 28.7 -> 29. Es un punto de partida,
    // no algo que se pueda verificar sin escuchar el juego — se afina jugando.
    constexpr int FRAMES_PER_BEAT = 29;
    constexpr int APPROACH_FRAMES = FRAMES_PER_BEAT * 3;
    // Silencio antes de la primera nota: sin esto, una nota en beat 0 nace ya
    // pegada al anillo (sin tiempo de acercarse) justo cuando arranca/reinicia.
    constexpr int LEAD_IN_FRAMES = APPROACH_FRAMES;
    // shortcut: ventanas generosas mientras el tempo no esté afinado de
    // oído — apretar cuando la calibración esté verificada jugando.
    constexpr int MISS_FRAMES = 14;
    constexpr int PERFECT_FRAMES = 4;
    constexpr int GREAT_FRAMES = 9;
    constexpr int MAX_HP = 5;
    constexpr int EUPHORIA_MAX = 100;
    constexpr int EUPHORIA_BAR_SEGMENTS = 10;
    constexpr int EUPHORIA_FRAMES = FRAMES_PER_BEAT * 16;
    constexpr int MISS_EUPHORIA_PENALTY = 40;
    constexpr bn::fixed HIT_DISTANCE = 40;

    struct score_tier
    {
        int points;
        int euphoria;
        const char* label;
        int count_index; // 0 excellent, 1 great, 2 good — para la precisión
    };

    constexpr score_tier PERFECT_TIER{ 300, 22, "EXCELLENT", 0 };
    constexpr score_tier GREAT_TIER{ 200, 10, "GREAT", 1 };
    constexpr score_tier GOOD_TIER{ 100, 0, "GOOD", 2 };
    // Toques intermedios de una ráfaga (fila de 3): el último paga GREAT.
    constexpr score_tier TAP_TIER{ 100, 8, "TAP", 2 };

    // Rango por precisión al terminar una canción (0 = sin rango guardado).
    constexpr const char* RANK_NAMES[] = { "", "C", "B", "A", "S" };
    constexpr int JUDGEMENT_FRAMES = 24;

    struct song_info
    {
        const char* name;
        bn::music_item music;
        bn::color zone_color;
        int enemy_set;
        int hero;
    };

    // Todas CC0, del mismo pack (Haydenwoffle .it Soundtrack, Spring+VinsCool,
    // https://opengameart.org/content/haydenwoffle-it-soundtrack). La canción
    // es un eje aparte de la dificultad: cualquiera de las tres usa el mismo
    // CHART_FACIL/NORMAL/DIFICIL de chart.h. zone_color es el "fondo especial"
    // de cada canción (pradera/cueva/cueva distinta) — sin arte de fondo
    // nuevo todavía, solo color, mismo truco que ya usa el menú.
    // "BOSS" es minor_boss_r.it, licencia CC-BY 4.0 (Spring/Spring Enterprises,
    // https://opengameart.org/content/minor-boss-r) — ya venía con el propio
    // repo de Butano, igual que battle_clean.it.
    // enemy_set/hero dan identidad por zona: 0 = mazmorra (bat/rat/
    // skeleton/slime, ninja), 1 = pradera (bichos, elfo), 2 = cripta
    // (calavera/fantasma, ninja). Ver ENEMY_SETS y HEROES más abajo.
    constexpr song_info SONG_LIST[] = {
        { "BATTLE", bn::music_items::battle_clean, bn::color(10, 6, 4), 0, 0 },
        { "DOGGO", bn::music_items::doggo_clean, bn::color(4, 12, 5), 1, 1 },
        { "PUPPYLAND", bn::music_items::puppyland_clean, bn::color(5, 7, 11), 1, 1 },
        { "FOREST", bn::music_items::forest_clean, bn::color(3, 10, 4), 1, 1 },
        { "GLIMMER", bn::music_items::glimmer_clean, bn::color(9, 9, 2), 0, 0 },
        { "HIDEOUT", bn::music_items::hideout_clean, bn::color(7, 7, 7), 2, 0 },
        { "PRISON", bn::music_items::prison_clean, bn::color(8, 3, 3), 2, 0 },
        { "VICTORY", bn::music_items::victory_clean, bn::color(11, 9, 3), 0, 1 },
        { "BOSS", bn::music_items::minor_boss_r, bn::color(9, 2, 2), 2, 0 },
    };
    constexpr int SONG_LIST_COUNT = sizeof(SONG_LIST) / sizeof(SONG_LIST[0]);
    constexpr int VISIBLE_SONGS = 4;
    // La última canción es el BOSS: juega un escalón de dificultad más
    // arriba de lo elegido (DIFICIL usa el chart especial difficulty::boss).
    constexpr int BOSS_SONG_INDEX = SONG_LIST_COUNT - 1;

    constexpr const char* DIFFICULTY_NAMES[] = { "FACIL", "NORMAL", "DIFICIL" };
    constexpr int DIFFICULTY_COUNT = sizeof(DIFFICULTY_NAMES) / sizeof(DIFFICULTY_NAMES[0]);

    constexpr const char* ENDED_OPTIONS[] = { "REINTENTAR", "MENU" };
    constexpr int ENDED_OPTION_COUNT = sizeof(ENDED_OPTIONS) / sizeof(ENDED_OPTIONS[0]);

    constexpr const char* PAUSE_OPTIONS[] = { "CONTINUAR", "SALIR" };
    constexpr int PAUSE_OPTION_COUNT = sizeof(PAUSE_OPTIONS) / sizeof(PAUSE_OPTIONS[0]);

    constexpr const char* MAIN_MENU_OPTIONS[] = { "JUGAR", "COMO JUGAR", "CONFIGURACION", "CREDITOS" };
    constexpr int MAIN_MENU_OPTION_COUNT = sizeof(MAIN_MENU_OPTIONS) / sizeof(MAIN_MENU_OPTIONS[0]);

    constexpr const char* HELP_LINES[] = {
        "PEGA CON LA CRUCETA",
        "AL PISAR EL ANILLO",
        "FILA DE 3: TOCA LA",
        "CRUCETA 3 VECES",
        "EUFORIA LLENA: L+R",
        "PUNTOS DOBLES UN RATO",
        "CORAZON: PEGALE Y CURA",
        "B PARA VOLVER",
    };
    constexpr int HELP_LINE_COUNT = sizeof(HELP_LINES) / sizeof(HELP_LINES[0]);

    constexpr const char* CREDITS_LINES[] = {
        "MUSICA: SPRING+VINSCOOL",
        "(OPENGAMEART, CC0)",
        "ARTE: TOADZILLART",
        "(ITCH.IO)",
        "SFX: SUBSPACEAUDIO",
        "(OPENGAMEART, CC0)",
        "",
        "B PARA VOLVER",
    };
    constexpr int CREDITS_LINE_COUNT = sizeof(CREDITS_LINES) / sizeof(CREDITS_LINES[0]);

    // Récord por canción y dificultad, persistido en SRAM. magic detecta la
    // primera vez que corre el juego en un cartucho/batería nueva (SRAM sin
    // inicializar, basura de fábrica) para no mostrar un récord falso.
    // 53: se sumó latency al layout — el magic nuevo descarta saves con
    // layouts viejos (igual que en los cambios anteriores).
    constexpr int SAVE_MAGIC = 0x474F4C53;

    // Tope del ajuste de latencia, en frames (~17 ms cada uno).
    constexpr int LATENCY_LIMIT = 15;

    struct save_data
    {
        int magic;
        int high_scores[SONG_LIST_COUNT][DIFFICULTY_COUNT];
        int ranks[SONG_LIST_COUNT][DIFFICULTY_COUNT]; // índice en RANK_NAMES
        int stars[SONG_LIST_COUNT][DIFFICULTY_COUNT]; // 1 = full combo logrado
        int latency; // frames que se restan al reloj de juicio (+ = pegas tarde)
    };

    bn::fixed_point lane_direction(lane ln)
    {
        switch (ln)
        {
        case lane::up: return bn::fixed_point(0, -1);
        case lane::down: return bn::fixed_point(0, 1);
        case lane::left: return bn::fixed_point(-1, 0);
        case lane::right: return bn::fixed_point(1, 0);
        }
        return bn::fixed_point(0, 0);
    }

    bn::fixed lane_spawn_distance(lane)
    {
        // Misma distancia (y por lo tanto misma velocidad) para los 4 carriles:
        // 70 es lo más lejos que entra en la mitad vertical de pantalla (80px).
        return bn::fixed(70);
    }

    // Orden de carriles: up, down, left, right (igual que el enum lane).
    // El carril de arriba lleva siempre al enemigo volador del set.
    constexpr const bn::sprite_item* ENEMY_SETS[][4] = {
        { &bn::sprite_items::enemy_bat, &bn::sprite_items::enemy_rat,
          &bn::sprite_items::enemy_skeleton, &bn::sprite_items::enemy_slime },
        { &bn::sprite_items::enemy_fly, &bn::sprite_items::enemy_spider,
          &bn::sprite_items::enemy_goblin, &bn::sprite_items::enemy_slime },
        { &bn::sprite_items::enemy_flying_skull, &bn::sprite_items::enemy_ghost,
          &bn::sprite_items::enemy_skeleton, &bn::sprite_items::enemy_spider },
    };

    constexpr const bn::sprite_item* HEROES[] = {
        &bn::sprite_items::hero,
        &bn::sprite_items::hero_elf,
    };

    const bn::sprite_item& lane_sprite_item(lane ln, int enemy_set)
    {
        return *ENEMY_SETS[enemy_set][int(ln)];
    }

    bool lane_pressed(lane ln)
    {
        switch (ln)
        {
        case lane::up: return bn::keypad::up_pressed();
        case lane::down: return bn::keypad::down_pressed();
        case lane::left: return bn::keypad::left_pressed();
        case lane::right: return bn::keypad::right_pressed();
        }
        return false;
    }

    struct active_note
    {
        int chart_index;
        lane ln;
        enemy_type type;
        int hit_frame;
        int hold_end_frame;
        bn::optional<bn::sprite_ptr> sprite;
        // Solo ráfagas: unidades repetidas detrás de la cabeza; cada toque
        // revienta la del frente y las demás avanzan.
        bn::vector<bn::sprite_ptr, 2> trail;
    };

    void style_sprite(bn::sprite_ptr& sprite, enemy_type type, bool euphoria_active)
    {
        bn::sprite_palette_ptr palette = sprite.palette();
        if (type == enemy_type::heart)
        {
            // El corazón se queda con sus colores, incluso en euforia.
            palette.set_fade_intensity(0);
        }
        else if (euphoria_active)
        {
            palette.set_fade(bn::colors::yellow, 0.5);
        }
        else if (type == enemy_type::armored)
        {
            palette.set_fade(bn::colors::gray, 0.6);
        }
        else if (type == enemy_type::projectile)
        {
            palette.set_fade(bn::colors::magenta, 0.5);
        }
        else if (type == enemy_type::chain)
        {
            palette.set_fade(bn::colors::cyan, 0.5);
        }
        else
        {
            palette.set_fade_intensity(0);
        }
    }
}

enum class game_phase
{
    main_menu,
    song_select,
    playing,
    ended,
    credits,
    how_to_play,
    settings,
    paused,
};

int main()
{
    bn::core::init();

    save_data save;
    bn::sram::read(save);
    if (save.magic != SAVE_MAGIC)
    {
        save.magic = SAVE_MAGIC;
        for (int s = 0; s < SONG_LIST_COUNT; ++s)
        {
            for (int d = 0; d < DIFFICULTY_COUNT; ++d)
            {
                save.high_scores[s][d] = 0;
                save.ranks[s][d] = 0;
                save.stars[s][d] = 0;
            }
        }
        save.latency = 0;
        bn::sram::write(save);
    }

    bn::vector<chart_note, CHART_MAX_NOTES> facil_chart;
    bn::vector<chart_note, CHART_MAX_NOTES> normal_chart;
    bn::vector<chart_note, CHART_MAX_NOTES> dificil_chart;
    bn::vector<chart_note, CHART_MAX_NOTES> boss_chart;
    build_chart(difficulty::facil, facil_chart);
    build_chart(difficulty::normal, normal_chart);
    build_chart(difficulty::dificil, dificil_chart);
    build_chart(difficulty::boss, boss_chart);

    const auto chart_for = [&](int song_index, int diff_index) -> const bn::ivector<chart_note>&
    {
        // El BOSS juega un escalón más arriba: FACIL usa el chart NORMAL,
        // NORMAL el DIFICIL, y DIFICIL el chart especial de boss.
        if (song_index == BOSS_SONG_INDEX)
        {
            ++diff_index;
        }
        if (diff_index == 0)
        {
            return facil_chart;
        }
        if (diff_index == 1)
        {
            return normal_chart;
        }
        if (diff_index == 2)
        {
            return dificil_chart;
        }
        return boss_chart;
    };

    bn::sprite_text_generator text_generator(common::variable_8x16_sprite_font);

    bn::sprite_ptr hero_sprite = bn::sprite_items::hero.create_sprite(0, 0);
    // Héroe de la canción elegida (ninja o elfo); reset_game() lo actualiza.
    const bn::sprite_item* hero_item = &bn::sprite_items::hero;

    // Espada que aparece solo durante el golpe (sin arte de personaje armado:
    // es un sprite aparte que se posiciona junto al héroe en ese instante).
    bn::sprite_ptr sword_sprite = bn::sprite_items::weapon_sword.create_sprite(0, 0);
    sword_sprite.set_visible(false);

    // Arena pintada (piso, carriles y anillo de golpe con arcos por carril),
    // generada por assets-src/make_arena.py. El exterior del piso es
    // transparente, así se sigue viendo el zone_color por canción que
    // pone el loop principal.
    bn::regular_bg_ptr arena_bg = bn::regular_bg_items::arena.create_bg(0, 0);
    arena_bg.set_visible(false);

    // Tarjeta de menú (borde, sombra desplazada y barra de acento con los
    // colores de carril). Exterior transparente; en la pantalla de fin se
    // dibuja encima de la arena. z_order menor = delante, misma prioridad.
    bn::regular_bg_ptr panel_bg = bn::regular_bg_items::menu_panel.create_bg(0, 0);
    panel_bg.set_z_order(-1);

    // Logotipo RHYTHM CAVERNS (assets-src/make_logo.py), solo en el menú
    // principal — en las demás pantallas el título es texto chico.
    bn::regular_bg_ptr logo_bg = bn::regular_bg_items::logo.create_bg(0, 0);
    logo_bg.set_z_order(-2);

    bn::vector<bn::sprite_ptr, 20> title_line1;
    bn::vector<bn::sprite_ptr, 24> title_line2;

    // Título de texto para las pantallas sin logo (selección de canción):
    // -58 y no -66, justo debajo de la barra de acento de la tarjeta.
    const auto draw_title = [&]()
    {
        text_generator.generate(-56, -58, "RHYTHM CAVERNS", title_line1);
    };

    bn::vector<bn::sprite_ptr, 20> main_menu_line[MAIN_MENU_OPTION_COUNT];
    int main_menu_cursor = 0;

    const auto draw_main_menu = [&]()
    {
        for (int k = 0; k < MAIN_MENU_OPTION_COUNT; ++k)
        {
            main_menu_line[k].clear();
            bn::fixed y = -10 + k * 18;
            if (k == main_menu_cursor)
            {
                text_generator.generate(-56, y, ">", main_menu_line[k]);
            }
            text_generator.generate(-42, y, MAIN_MENU_OPTIONS[k], main_menu_line[k]);
        }

        title_line2.clear();
        text_generator.generate(-56, 66, "START PARA ELEGIR", title_line2);
    };

    bn::vector<bn::sprite_ptr, 28> credits_text[CREDITS_LINE_COUNT];

    const auto draw_credits = [&]()
    {
        for (int i = 0; i < CREDITS_LINE_COUNT; ++i)
        {
            credits_text[i].clear();
            text_generator.generate(-74, -56 + i * 16, CREDITS_LINES[i], credits_text[i]);
        }
    };

    const auto clear_credits = [&]()
    {
        for (int i = 0; i < CREDITS_LINE_COUNT; ++i)
        {
            credits_text[i].clear();
        }
    };

    bn::vector<bn::sprite_ptr, 28> help_text[HELP_LINE_COUNT];

    const auto draw_help = [&]()
    {
        for (int i = 0; i < HELP_LINE_COUNT; ++i)
        {
            help_text[i].clear();
            text_generator.generate(-74, -56 + i * 16, HELP_LINES[i], help_text[i]);
        }
    };

    const auto clear_help = [&]()
    {
        for (int i = 0; i < HELP_LINE_COUNT; ++i)
        {
            help_text[i].clear();
        }
    };

    // Configuración: por ahora solo el ajuste de latencia (en frames,
    // ~17 ms cada uno). Positivo = el juicio se corre hacia atrás, para
    // quien siente que pega tarde aunque vaya al ritmo.
    constexpr int SETTINGS_LINE_COUNT = 5;
    bn::vector<bn::sprite_ptr, 28> settings_text[SETTINGS_LINE_COUNT];

    const auto draw_settings = [&]()
    {
        bn::string<24> value_line = "LATENCIA: ";
        if (save.latency > 0)
        {
            value_line.append("+");
        }
        value_line.append(bn::to_string<4>(save.latency));
        const char* lines[SETTINGS_LINE_COUNT] = {
            value_line.c_str(),
            "IZQ/DER AJUSTA (17 MS)",
            "SI PEGAS TARDE SUBILA",
            "SI PEGAS ANTES BAJALA",
            "B GUARDA Y VUELVE",
        };
        constexpr int SETTINGS_Y[SETTINGS_LINE_COUNT] = { -48, -24, -8, 8, 40 };
        for (int i = 0; i < SETTINGS_LINE_COUNT; ++i)
        {
            settings_text[i].clear();
            text_generator.generate(-74, SETTINGS_Y[i], lines[i], settings_text[i]);
        }
    };

    const auto clear_settings = [&]()
    {
        for (int i = 0; i < SETTINGS_LINE_COUNT; ++i)
        {
            settings_text[i].clear();
        }
    };

    bn::vector<bn::sprite_ptr, 20> menu_line[VISIBLE_SONGS];
    bn::vector<bn::sprite_ptr, 20> difficulty_line;
    bn::vector<bn::sprite_ptr, 24> record_line;
    int menu_song = 0;
    int menu_difficulty = 1;

    const auto draw_menu = [&]()
    {
        int menu_scroll = menu_song - VISIBLE_SONGS / 2;
        if (menu_scroll < 0)
        {
            menu_scroll = 0;
        }
        if (menu_scroll > SONG_LIST_COUNT - VISIBLE_SONGS)
        {
            menu_scroll = SONG_LIST_COUNT - VISIBLE_SONGS;
        }

        for (int i = 0; i < VISIBLE_SONGS; ++i)
        {
            menu_line[i].clear();
            int k = menu_scroll + i;
            bn::fixed y = -40 + i * 16;
            if (k == menu_song)
            {
                text_generator.generate(-74, y, ">", menu_line[i]);
            }
            text_generator.generate(-60, y, SONG_LIST[k].name, menu_line[i]);
        }

        difficulty_line.clear();
        text_generator.generate(-56, 28, "<", difficulty_line);
        text_generator.generate(-40, 28, DIFFICULTY_NAMES[menu_difficulty], difficulty_line);
        text_generator.generate(32, 28, ">", difficulty_line);

        record_line.clear();
        bn::string<24> record_str = "RECORD ";
        record_str.append(bn::to_string<10>(save.high_scores[menu_song][menu_difficulty]));
        int saved_rank = save.ranks[menu_song][menu_difficulty];
        if (saved_rank > 0)
        {
            record_str.append(" ");
            record_str.append(RANK_NAMES[saved_rank]);
        }
        if (save.stars[menu_song][menu_difficulty])
        {
            record_str.append("*"); // estrella: full combo logrado
        }
        text_generator.generate(-56, 46, record_str, record_line);

        title_line2.clear();
        text_generator.generate(-74, 66, "ARRIBA/ABAJO+L/R+START", title_line2);
    };

    const auto clear_song_select = [&]()
    {
        difficulty_line.clear();
        record_line.clear();
        for (int k = 0; k < VISIBLE_SONGS; ++k)
        {
            menu_line[k].clear();
        }
    };

    draw_main_menu();

    // Tema de menú: reusa una canción existente en vez de sumar un archivo
    // de audio más — suena en main_menu/song_select/credits, reset_game()
    // la reemplaza por la canción elegida al entrar a jugar. battle_clean
    // y no hayden_clean: hayden cuelga maxmod al llegar a cierto patrón
    // (el juego entero se congela); battle es el módulo que venía recortado
    // y validado por el propio repo de Butano.
    const auto play_menu_music = [&]()
    {
        bn::music::play(bn::music_items::battle_clean, 1, true);
    };
    play_menu_music();

    bn::vector<bn::sprite_ptr, 24> ended_line[ENDED_OPTION_COUNT];
    int ended_cursor = 0;

    const auto draw_ended_menu = [&]()
    {
        for (int k = 0; k < ENDED_OPTION_COUNT; ++k)
        {
            ended_line[k].clear();
            bn::fixed y = 40 + k * 18;
            if (k == ended_cursor)
            {
                text_generator.generate(-56, y, ">", ended_line[k]);
            }
            text_generator.generate(-42, y, ENDED_OPTIONS[k], ended_line[k]);
        }
    };

    // El menú de pausa reusa ended_line/ended_cursor (fases disjuntas).
    const auto draw_pause_menu = [&]()
    {
        for (int k = 0; k < PAUSE_OPTION_COUNT; ++k)
        {
            ended_line[k].clear();
            bn::fixed y = 40 + k * 18;
            if (k == ended_cursor)
            {
                text_generator.generate(-56, y, ">", ended_line[k]);
            }
            text_generator.generate(-42, y, PAUSE_OPTIONS[k], ended_line[k]);
        }
    };

    bn::vector<bn::sprite_ptr, 16> score_text;
    bn::vector<bn::sprite_ptr, 8> combo_text;
    bn::vector<bn::sprite_ptr, 10> euphoria_text;

    // Corazones de HP: sprites fijos (se crean una sola vez), se tiñen
    // según cuántos quedan en vez de regenerar texto cada vez.
    bn::vector<bn::sprite_ptr, MAX_HP> heart_sprites;
    for (int i = 0; i < MAX_HP; ++i)
    {
        heart_sprites.push_back(bn::sprite_items::heart.create_sprite(64 + i * 11, -72));
    }
    bn::vector<bn::sprite_ptr, 56> message_text;
    bn::vector<bn::sprite_ptr, 16> judgement_text;
    int judgement_until = 0;

    // 12 y no 8: las cadenas ocupan su lugar durante todo el hold (hasta
    // ~6 beats entre acercarse y sostenerse) y en DIFICIL conviven con
    // una nota por beat — con 8 se llenaba y las notas aparecían tarde.
    bn::vector<active_note, 12> active_notes;

    // Animación de salida: al resolverse, el sprite no desaparece en seco —
    // vuela hacia afuera (derrotado) o se mete hasta el héroe (te pegó).
    struct dying_enemy
    {
        bn::sprite_ptr sprite;
        lane ln;
        int start_frame;
        bool outward;
    };
    bn::vector<dying_enemy, 8> dying_enemies;

    game_phase phase = game_phase::main_menu;
    int selected_song_index = 0;
    int selected_difficulty = 1;
    int frame = 0;
    int next_chart_index = 0;
    int score = 0;
    int combo = 0;
    int hp = MAX_HP;
    int miss_count = 0;
    int tier_counts[3] = { 0, 0, 0 }; // excellent / great / good
    int combo_pop_until = 0;
    int euphoria_meter = 0;
    bool euphoria_active = false;
    int euphoria_ends_frame = 0;
    int hero_flash_until = 0;
    bn::color hero_flash_color = bn::colors::white;
    int hero_motion_start = 0;
    lane hero_motion_lane = lane::up;
    bool hero_hit = true;
    int shake_until = 0;

    int shown_score = -1;
    int shown_combo = -1;
    int shown_euphoria = -1;

    const auto reset_game = [&]()
    {
        active_notes.clear();
        dying_enemies.clear();
        shake_until = 0;
        frame = 0;
        next_chart_index = 0;
        score = 0;
        combo = 0;
        hp = MAX_HP;
        miss_count = 0;
        tier_counts[0] = tier_counts[1] = tier_counts[2] = 0;
        combo_pop_until = 0;
        euphoria_meter = 0;
        euphoria_active = false;
        euphoria_ends_frame = 0;
        hero_flash_until = 0;
        hero_hit = true;
        judgement_until = 0;
        judgement_text.clear();
        hero_item = HEROES[SONG_LIST[selected_song_index].hero];
        hero_sprite.set_item(*hero_item, 0);
        hero_sprite.set_position(0, 0);
        bn::sprite_palette_ptr reset_palette = hero_sprite.palette();
        reset_palette.set_fade_intensity(0);
        shown_score = -1;
        shown_combo = -1;
        shown_euphoria = -1;
        score_text.clear();
        combo_text.clear();
        euphoria_text.clear();
        bn::music::play(SONG_LIST[selected_song_index].music, 1, true);
    };

    // Guarda récord, mejor rango y estrella de full combo (el rango y la
    // estrella solo cuentan si la canción se terminó). Devuelve si hubo
    // récord nuevo de puntaje.
    const auto save_results = [&](bool cleared, int rank_index, bool full_combo)
    {
        int s = selected_song_index;
        int d = selected_difficulty;
        bool new_record = false;
        bool dirty = false;
        if (score > save.high_scores[s][d])
        {
            save.high_scores[s][d] = score;
            new_record = true;
            dirty = true;
        }
        if (cleared && rank_index > save.ranks[s][d])
        {
            save.ranks[s][d] = rank_index;
            dirty = true;
        }
        if (cleared && full_combo && !save.stars[s][d])
        {
            save.stars[s][d] = 1;
            dirty = true;
        }
        if (dirty)
        {
            bn::sram::write(save);
        }
        return new_record;
    };

    // Precisión 0-100 ponderada por calidad de golpe, y rango S/A/B/C.
    const auto accuracy_percent = [&]()
    {
        int resolved = tier_counts[0] + tier_counts[1] + tier_counts[2] + miss_count;
        if (resolved == 0)
        {
            return 0;
        }
        return (tier_counts[0] * 100 + tier_counts[1] * 70 + tier_counts[2] * 40) / resolved;
    };

    const auto resolve_hit = [&](active_note& note, const score_tier& tier)
    {
        tier_counts[tier.count_index] += 1;
        combo += 1;
        combo_pop_until = frame + 10;
        int multiplier_percent = 100 + (combo / 10) * 25;
        int gain = tier.points * multiplier_percent / 100;
        if (euphoria_active)
        {
            gain *= 2;
        }
        score += gain;

        euphoria_meter += tier.euphoria;
        if (euphoria_meter > EUPHORIA_MAX)
        {
            euphoria_meter = EUPHORIA_MAX;
        }

        hero_flash_until = frame + 10;
        hero_flash_color = bn::colors::yellow;
        hero_motion_start = frame;
        hero_motion_lane = note.ln;
        hero_hit = true;

        judgement_text.clear();
        text_generator.generate(-40, 50, tier.label, judgement_text);
        judgement_until = frame + JUDGEMENT_FRAMES;

        bn::sound_items::sfx_hit.play();
    };

    const auto register_miss = [&](active_note&)
    {
        combo = 0;
        hp -= 1;
        miss_count += 1;

        euphoria_meter -= MISS_EUPHORIA_PENALTY;
        if (euphoria_meter < 0)
        {
            euphoria_meter = 0;
        }
        euphoria_active = false;

        hero_flash_until = frame + 12;
        hero_flash_color = bn::colors::red;
        hero_motion_start = frame;
        hero_hit = false;

        judgement_text.clear();
        text_generator.generate(-16, 50, "BAD", judgement_text);
        judgement_until = frame + JUDGEMENT_FRAMES;
        shake_until = frame + 8;

        bn::sound_items::sfx_miss.play();
    };

    // Pasa el sprite de una nota que se resuelve a la lista de "muriendo",
    // para animar su salida unos frames antes de destruirlo.
    const auto detach_sprite = [&](active_note& note, bool outward)
    {
        if (!dying_enemies.full())
        {
            dying_enemies.push_back({ bn::move(*note.sprite), note.ln, frame, outward });
        }
    };

    while (true)
    {
        if (phase == game_phase::main_menu)
        {
            if (bn::keypad::up_pressed())
            {
                main_menu_cursor = (main_menu_cursor + MAIN_MENU_OPTION_COUNT - 1) % MAIN_MENU_OPTION_COUNT;
                draw_main_menu();
                bn::sound_items::sfx_menu_move.play();
            }
            else if (bn::keypad::down_pressed())
            {
                main_menu_cursor = (main_menu_cursor + 1) % MAIN_MENU_OPTION_COUNT;
                draw_main_menu();
                bn::sound_items::sfx_menu_move.play();
            }
            // A confirma igual que START en todos los menús (en consola real
            // es el botón natural de "aceptar"); jugando, A queda reservado.
            else if (bn::keypad::start_pressed() || bn::keypad::a_pressed())
            {
                for (int k = 0; k < MAIN_MENU_OPTION_COUNT; ++k)
                {
                    main_menu_line[k].clear();
                }
                bn::sound_items::sfx_menu_select.play();

                if (main_menu_cursor == 0)
                {
                    draw_title();
                    draw_menu();
                    phase = game_phase::song_select;
                }
                else
                {
                    // Sin título en ayuda/créditos: su línea choca con la
                    // primera de texto.
                    title_line1.clear();
                    title_line2.clear();
                    if (main_menu_cursor == 1)
                    {
                        draw_help();
                        phase = game_phase::how_to_play;
                    }
                    else if (main_menu_cursor == 2)
                    {
                        draw_settings();
                        phase = game_phase::settings;
                    }
                    else
                    {
                        draw_credits();
                        phase = game_phase::credits;
                    }
                }
            }
        }
        else if (phase == game_phase::settings)
        {
            if (bn::keypad::left_pressed() || bn::keypad::right_pressed())
            {
                int delta = bn::keypad::left_pressed() ? -1 : 1;
                int adjusted = save.latency + delta;
                if (adjusted >= -LATENCY_LIMIT && adjusted <= LATENCY_LIMIT)
                {
                    save.latency = adjusted;
                    draw_settings();
                    bn::sound_items::sfx_menu_move.play();
                }
            }
            else if (bn::keypad::b_pressed())
            {
                clear_settings();
                bn::sram::write(save);
                draw_main_menu();
                phase = game_phase::main_menu;
                bn::sound_items::sfx_menu_select.play();
            }
        }
        else if (phase == game_phase::credits || phase == game_phase::how_to_play)
        {
            if (bn::keypad::b_pressed())
            {
                if (phase == game_phase::credits)
                {
                    clear_credits();
                }
                else
                {
                    clear_help();
                }
                draw_main_menu();
                phase = game_phase::main_menu;
                bn::sound_items::sfx_menu_select.play();
            }
        }
        else if (phase == game_phase::song_select)
        {
            if (bn::keypad::up_pressed())
            {
                menu_song = (menu_song + SONG_LIST_COUNT - 1) % SONG_LIST_COUNT;
                draw_menu();
                bn::sound_items::sfx_menu_move.play();
            }
            else if (bn::keypad::down_pressed())
            {
                menu_song = (menu_song + 1) % SONG_LIST_COUNT;
                draw_menu();
                bn::sound_items::sfx_menu_move.play();
            }
            else if (bn::keypad::l_pressed())
            {
                menu_difficulty = (menu_difficulty + DIFFICULTY_COUNT - 1) % DIFFICULTY_COUNT;
                draw_menu();
                bn::sound_items::sfx_menu_move.play();
            }
            else if (bn::keypad::r_pressed())
            {
                menu_difficulty = (menu_difficulty + 1) % DIFFICULTY_COUNT;
                draw_menu();
                bn::sound_items::sfx_menu_move.play();
            }
            else if (bn::keypad::b_pressed())
            {
                clear_song_select();
                title_line1.clear();
                draw_main_menu();
                phase = game_phase::main_menu;
                bn::sound_items::sfx_menu_select.play();
            }
            else if (bn::keypad::start_pressed() || bn::keypad::a_pressed())
            {
                title_line1.clear();
                title_line2.clear();
                clear_song_select();
                selected_song_index = menu_song;
                selected_difficulty = menu_difficulty;
                reset_game();
                phase = game_phase::playing;
                bn::sound_items::sfx_menu_select.play();
            }
        }
        else if (phase == game_phase::paused)
        {
            bool start_now = bn::keypad::start_pressed() || bn::keypad::a_pressed();
            if (bn::keypad::up_pressed() || bn::keypad::down_pressed())
            {
                ended_cursor = (ended_cursor + 1) % PAUSE_OPTION_COUNT;
                draw_pause_menu();
                bn::sound_items::sfx_menu_move.play();
            }
            else if (bn::keypad::b_pressed() || (start_now && ended_cursor == 0))
            {
                // Continuar: B es el atajo, START sobre CONTINUAR también.
                message_text.clear();
                for (int k = 0; k < PAUSE_OPTION_COUNT; ++k)
                {
                    ended_line[k].clear();
                }
                bn::music::resume();
                phase = game_phase::playing;
                bn::sound_items::sfx_menu_select.play();
            }
            else if (start_now)
            {
                // Salir al menú de canciones, limpiando la partida a medias.
                message_text.clear();
                judgement_text.clear();
                for (int k = 0; k < PAUSE_OPTION_COUNT; ++k)
                {
                    ended_line[k].clear();
                }
                active_notes.clear();
                dying_enemies.clear();
                score_text.clear();
                combo_text.clear();
                euphoria_text.clear();
                bn::music::resume();
                play_menu_music();
                draw_title();
                draw_menu();
                phase = game_phase::song_select;
                bn::sound_items::sfx_menu_select.play();
            }
        }
        else if (phase == game_phase::ended)
        {
            if (bn::keypad::up_pressed())
            {
                ended_cursor = (ended_cursor + ENDED_OPTION_COUNT - 1) % ENDED_OPTION_COUNT;
                draw_ended_menu();
                bn::sound_items::sfx_menu_move.play();
            }
            else if (bn::keypad::down_pressed())
            {
                ended_cursor = (ended_cursor + 1) % ENDED_OPTION_COUNT;
                draw_ended_menu();
                bn::sound_items::sfx_menu_move.play();
            }
            else if (bn::keypad::start_pressed() || bn::keypad::a_pressed())
            {
                message_text.clear();
                for (int k = 0; k < ENDED_OPTION_COUNT; ++k)
                {
                    ended_line[k].clear();
                }
                bn::sound_items::sfx_menu_select.play();

                if (ended_cursor == 0)
                {
                    reset_game();
                    phase = game_phase::playing;
                }
                else
                {
                    score_text.clear();
                    combo_text.clear();
                    euphoria_text.clear();
                    draw_title();
                    draw_menu();
                    phase = game_phase::song_select;
                    play_menu_music();
                }
            }
        }
        else
        {
            const bn::ivector<chart_note>& current_chart = chart_for(selected_song_index, selected_difficulty);
            while (next_chart_index < current_chart.size() && !active_notes.full() &&
                   LEAD_IN_FRAMES + current_chart[next_chart_index].beat * FRAMES_PER_BEAT - frame <= APPROACH_FRAMES)
            {
                const chart_note& note = current_chart[next_chart_index];
                active_note active{};
                active.chart_index = next_chart_index;
                active.ln = note.ln;
                active.type = note.type;
                active.hit_frame = LEAD_IN_FRAMES + note.beat * FRAMES_PER_BEAT;
                active.hold_end_frame = active.hit_frame + note.hold_beats * FRAMES_PER_BEAT;
                const bn::sprite_item& enemy_item = note.type == enemy_type::heart
                        ? bn::sprite_items::heart_pickup
                        : lane_sprite_item(note.ln, SONG_LIST[selected_song_index].enemy_set);
                active.sprite = enemy_item.create_sprite(
                        lane_direction(note.ln) * lane_spawn_distance(note.ln));
                if (note.type == enemy_type::chain)
                {
                    for (int t = 0; t < 2; ++t)
                    {
                        active.trail.push_back(enemy_item.create_sprite(
                                lane_direction(note.ln) * (lane_spawn_distance(note.ln) + 14 * (t + 1))));
                    }
                }
                active_notes.push_back(bn::move(active));
                ++next_chart_index;
            }

            // Paso de animación a medio beat: todos los enemigos (y el héroe
            // en reposo) marchan sincronizados con la música.
            int anim_step = (frame * 2 / FRAMES_PER_BEAT) % 2;

            // Reloj de juicio con el ajuste de latencia de CONFIGURACION:
            // solo afecta cuándo se juzgan los golpes, no los visuales.
            int judge_frame = frame - save.latency;

            int i = 0;
            while (i < active_notes.size())
            {
                active_note& note = active_notes[i];
                int diff_frames = note.hit_frame - frame;
                bool remove = false;

                // La ráfaga espera en el anillo hasta que vence su ventana
                // de mash (hold_end_frame); lo demás, la ventana normal.
                int deadline = note.type == enemy_type::chain
                        ? note.hold_end_frame
                        : note.hit_frame + MISS_FRAMES;
                if (judge_frame > deadline)
                {
                    if (note.type == enemy_type::heart)
                    {
                        // Perder el corazón no castiga: era un premio.
                        remove = true;
                    }
                    else
                    {
                        detach_sprite(note, false);
                        register_miss(note);
                        remove = true;
                    }
                }

                if (remove)
                {
                    active_notes.erase(active_notes.begin() + i);
                    continue;
                }

                bn::fixed progress = bn::fixed(diff_frames) / APPROACH_FRAMES;
                if (progress < 0)
                {
                    progress = 0;
                }
                if (progress > 1)
                {
                    progress = 1;
                }

                bn::fixed distance = HIT_DISTANCE + progress * (lane_spawn_distance(note.ln) - HIT_DISTANCE);
                const bn::sprite_item& enemy_item = note.type == enemy_type::heart
                        ? bn::sprite_items::heart_pickup
                        : lane_sprite_item(note.ln, SONG_LIST[selected_song_index].enemy_set);
                note.sprite->set_position(lane_direction(note.ln) * distance);
                if (note.type != enemy_type::heart) // el corazón tiene 1 frame
                {
                    note.sprite->set_item(enemy_item, anim_step);
                }
                style_sprite(*note.sprite, note.type, euphoria_active);

                for (int t = 0; t < note.trail.size(); ++t)
                {
                    note.trail[t].set_position(lane_direction(note.ln) * (distance + 14 * (t + 1)));
                    // Frames alternados a lo largo de la fila: efecto onda.
                    note.trail[t].set_item(enemy_item, (anim_step + t + 1) % 2);
                }

                ++i;
            }

            constexpr lane ALL_LANES[] = { lane::up, lane::down, lane::left, lane::right };
            for (lane ln : ALL_LANES)
            {
                if (!lane_pressed(ln))
                {
                    continue;
                }

                int best_index = -1;
                int best_diff = MISS_FRAMES + 1;
                for (int j = 0; j < active_notes.size(); ++j)
                {
                    active_note& note = active_notes[j];
                    if (note.ln != ln)
                    {
                        continue;
                    }

                    int diff = note.hit_frame - judge_frame;
                    int abs_diff = diff < 0 ? -diff : diff;
                    if (note.type == enemy_type::chain)
                    {
                        // Ráfaga: elegible desde que entra en ventana hasta
                        // que vence el plazo de mash, con prioridad total.
                        if (judge_frame < note.hit_frame - MISS_FRAMES || judge_frame > note.hold_end_frame)
                        {
                            continue;
                        }
                        abs_diff = 0;
                    }
                    if (abs_diff <= MISS_FRAMES && abs_diff < best_diff)
                    {
                        best_diff = abs_diff;
                        best_index = j;
                    }
                }

                if (best_index < 0)
                {
                    continue;
                }

                active_note& note = active_notes[best_index];

                if (note.type == enemy_type::chain)
                {
                    // Mash estilo Muse Dash: cada toque revienta la unidad
                    // del frente y las de atrás avanzan; la última paga GREAT.
                    detach_sprite(note, true);
                    if (!note.trail.empty())
                    {
                        note.sprite = bn::move(note.trail[0]);
                        note.trail.erase(note.trail.begin());
                        resolve_hit(note, TAP_TIER);
                    }
                    else
                    {
                        resolve_hit(note, GREAT_TIER);
                        active_notes.erase(active_notes.begin() + best_index);
                    }
                    continue;
                }

                bool needs_a = note.type == enemy_type::armored;
                bool needs_b = note.type == enemy_type::projectile;

                if ((needs_a && !bn::keypad::a_held()) || (needs_b && !bn::keypad::b_held()))
                {
                    detach_sprite(note, false);
                    register_miss(note);
                }
                else
                {
                    const score_tier& tier = best_diff <= PERFECT_FRAMES ? PERFECT_TIER
                            : best_diff <= GREAT_FRAMES ? GREAT_TIER : GOOD_TIER;
                    bool heals = note.type == enemy_type::heart;
                    detach_sprite(note, true);
                    resolve_hit(note, tier);
                    if (heals && hp < MAX_HP)
                    {
                        ++hp;
                    }
                }

                active_notes.erase(active_notes.begin() + best_index);
            }

            if (euphoria_active && frame > euphoria_ends_frame)
            {
                euphoria_active = false;
            }
            else if (!euphoria_active && euphoria_meter >= EUPHORIA_MAX &&
                     bn::keypad::l_held() && bn::keypad::r_held())
            {
                euphoria_meter = 0;
                euphoria_active = true;
                euphoria_ends_frame = frame + EUPHORIA_FRAMES;
            }

            if (hp <= 0)
            {
                phase = game_phase::ended;
                bn::music::stop();
                bool new_record = save_results(false, 0, false);
                message_text.clear();
                text_generator.generate(-48, -28, "GAME OVER", message_text);
                bn::string<20> acc_line = "PRECISION ";
                acc_line.append(bn::to_string<4>(accuracy_percent()));
                acc_line.append("%");
                text_generator.generate(-48, -10, acc_line, message_text);
                if (new_record)
                {
                    text_generator.generate(-56, 8, "NUEVO RECORD", message_text);
                }
                ended_cursor = 0;
                draw_ended_menu();
            }
            else if (next_chart_index >= current_chart.size() && active_notes.empty())
            {
                phase = game_phase::ended;
                bn::music::stop();
                int accuracy = accuracy_percent();
                int rank_index = accuracy >= 95 ? 4 : accuracy >= 85 ? 3 : accuracy >= 70 ? 2 : 1;
                bool full_combo = miss_count == 0;
                bool new_record = save_results(true, rank_index, full_combo);
                message_text.clear();
                text_generator.generate(-24, -28, "LISTO", message_text);
                bn::string<16> rank_line = "RANGO ";
                rank_line.append(RANK_NAMES[rank_index]);
                if (full_combo)
                {
                    rank_line.append(" *"); // estrella de full combo
                }
                text_generator.generate(-32, -10, rank_line, message_text);
                bn::string<20> acc_line = "PRECISION ";
                acc_line.append(bn::to_string<4>(accuracy));
                acc_line.append("%");
                text_generator.generate(-48, 8, acc_line, message_text);
                if (new_record)
                {
                    text_generator.generate(-56, 26, "NUEVO RECORD", message_text);
                }
                ended_cursor = 0;
                draw_ended_menu();
            }

            // Pausa al final del frame (si la partida sigue viva): así ningún
            // otro código de juego corre con la música ya pausada.
            if (phase == game_phase::playing && bn::keypad::start_pressed())
            {
                phase = game_phase::paused;
                bn::music::pause();
                message_text.clear();
                text_generator.generate(-24, -28, "PAUSA", message_text);
                ended_cursor = 0;
                draw_pause_menu();
                bn::sound_items::sfx_menu_select.play();
            }
        }

        // Los enemigos "muriendo" siguen animándose aunque la partida termine
        // en ese mismo frame (por eso no va dentro de la fase playing).
        {
            int d = 0;
            while (d < dying_enemies.size())
            {
                dying_enemy& dying = dying_enemies[d];
                int age = frame - dying.start_frame;
                if (age > (dying.outward ? 12 : 8))
                {
                    dying_enemies.erase(dying_enemies.begin() + d);
                    continue;
                }
                bn::fixed distance = dying.outward ? HIT_DISTANCE + age * 7 : HIT_DISTANCE - age * 5;
                dying.sprite.set_position(lane_direction(dying.ln) * distance);
                ++d;
            }
        }

        if (!judgement_text.empty() && frame > judgement_until)
        {
            judgement_text.clear();
        }

        // Fondo especial: un color propio para el menú, y uno por canción
        // mientras se juega (pradera/cueva/cueva distinta), para no
        // restarle contraste al anillo con algo demasiado claro.
        if (phase == game_phase::playing)
        {
            bn::bg_palettes::set_transparent_color(SONG_LIST[selected_song_index].zone_color);
        }
        else
        {
            // El fondo oscuro del juego (#19132d) detrás de la tarjeta.
            bn::bg_palettes::set_transparent_color(bn::color(3, 2, 5));
        }

        // La arena solo tiene sentido jugando (y de fondo en la pantalla de
        // fin); en los menús se esconde y queda el color plano de siempre.
        arena_bg.set_visible(phase == game_phase::playing || phase == game_phase::ended
                || phase == game_phase::paused);
        panel_bg.set_visible(phase != game_phase::playing);
        logo_bg.set_visible(phase == game_phase::main_menu);

        // Temblor de la arena al fallar.
        if (phase == game_phase::playing && frame < shake_until)
        {
            arena_bg.set_position(frame % 2 == 0 ? 2 : -2, frame % 2 == 0 ? -1 : 1);
        }
        else
        {
            arena_bg.set_position(0, 0);
        }

        // Pulso de beat: el escenario se ilumina en cada bombo y decae;
        // la euforia lo deja con brillo extra sostenido.
        if (phase == game_phase::playing)
        {
            int since_beat = frame % FRAMES_PER_BEAT;
            bn::fixed brightness = since_beat < 6 ? bn::fixed(6 - since_beat) / 40 : bn::fixed(0);
            if (euphoria_active)
            {
                brightness += bn::fixed(0.08);
            }
            bn::bg_palettes::set_brightness(brightness);
        }
        else
        {
            bn::bg_palettes::set_brightness(0);
        }

        bn::sprite_palette_ptr hero_palette = hero_sprite.palette();
        if (frame < hero_flash_until)
        {
            hero_palette.set_fade(hero_flash_color, 0.6);

            bn::fixed t = bn::fixed(frame - hero_motion_start) / (hero_flash_until - hero_motion_start);
            if (hero_hit)
            {
                bn::fixed punch = t < bn::fixed(0.5) ? t * 2 : (1 - t) * 2;
                hero_sprite.set_position(lane_direction(hero_motion_lane) * (bn::fixed(16) * punch));
                hero_sprite.set_item(*hero_item, (frame / 2) % 3);
                hero_sprite.set_horizontal_flip(hero_motion_lane == lane::right);

                sword_sprite.set_visible(true);
                sword_sprite.set_position(lane_direction(hero_motion_lane) * (bn::fixed(22) * punch));
            }
            else
            {
                bn::fixed shake = (frame % 2 == 0) ? bn::fixed(4) : bn::fixed(-4);
                hero_sprite.set_position(shake, 0);
                sword_sprite.set_visible(false);
            }
        }
        else
        {
            // En reposo marcha en el lugar a medio beat; jugando, además da
            // un saltito que decae en cada bombo (mismo pulso que el brillo).
            hero_sprite.set_item(*hero_item, (frame * 2 / FRAMES_PER_BEAT) % 2);
            if (phase == game_phase::playing || phase == game_phase::paused)
            {
                int since_beat = frame % FRAMES_PER_BEAT;
                bn::fixed bob = since_beat < 6 ? bn::fixed(6 - since_beat) / 3 : bn::fixed(0);
                hero_sprite.set_position(0, -bob);
            }
            else
            {
                hero_sprite.set_position(88, 14);
            }
            sword_sprite.set_visible(false);

            if (euphoria_active)
            {
                hero_palette.set_fade(bn::colors::yellow, 0.5);
            }
            else
            {
                hero_palette.set_fade_intensity(0);
            }
        }

        if (phase == game_phase::playing || phase == game_phase::ended
                || phase == game_phase::paused)
        {
            if (score != shown_score)
            {
                shown_score = score;
                score_text.clear();
                text_generator.generate(-116, -74, bn::to_string<16>(score), score_text);
            }
            if (combo != shown_combo)
            {
                shown_combo = combo;
                combo_text.clear();
                if (combo >= 2)
                {
                    text_generator.generate(-16, -74, bn::to_string<8>(combo), combo_text);
                }
            }

            // shortcut: rebote de posición en vez de set_scale() — escalar
            // pone al sprite en modo affine, que en GBA usa una llamada a la
            // BIOS real (ObjAffineSet); algunos emuladores/flashcarts la
            // piden explícita. Esto da el mismo "pop" sin esa dependencia.
            bn::fixed combo_bounce = 0;
            if (frame < combo_pop_until)
            {
                bn::fixed t = bn::fixed(combo_pop_until - frame) / 10;
                combo_bounce = t * 5;
            }
            for (bn::sprite_ptr& combo_sprite : combo_text)
            {
                combo_sprite.set_y(-74 - combo_bounce);
            }

            // Todos los corazones comparten UNA paleta (Butano deduplica
            // paletas idénticas), así que teñir el perdido teñía todos
            // (bug visto en consola real: 1 golpe "apagaba" las 5 vidas).
            // El corazón perdido se oculta en vez de teñirse.
            for (int i = 0; i < MAX_HP; ++i)
            {
                heart_sprites[i].set_visible(i < hp);
            }

            if (euphoria_meter != shown_euphoria)
            {
                shown_euphoria = euphoria_meter;
                euphoria_text.clear();
                int filled = euphoria_meter * EUPHORIA_BAR_SEGMENTS / EUPHORIA_MAX;
                bn::string<16> bar = "[";
                bar.append(filled, '#');
                bar.append(EUPHORIA_BAR_SEGMENTS - filled, '-');
                bar.append("]");
                text_generator.generate(-56, -58, bar, euphoria_text);
            }
        }
        else
        {
            for (bn::sprite_ptr& heart_sprite : heart_sprites)
            {
                heart_sprite.set_visible(false);
            }
        }

        bool euphoria_ready = euphoria_meter >= EUPHORIA_MAX && !euphoria_active;
        for (bn::sprite_ptr& bar_sprite : euphoria_text)
        {
            bn::sprite_palette_ptr bar_palette = bar_sprite.palette();
            if (euphoria_ready)
            {
                bar_palette.set_fade(bn::colors::yellow, 0.6);
            }
            else
            {
                bar_palette.set_fade_intensity(0);
            }
        }

        // En pausa el reloj de juego se congela: notas, animaciones y
        // timers quedan exactamente donde estaban al reanudar.
        if (phase != game_phase::paused)
        {
            ++frame;
        }
        bn::core::update();
    }
}
