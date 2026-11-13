/*
 * Golpe a tiempo — mecánicas reales en GBA (sin música todavía: el reloj es
 * el contador de frames; cuando exista un módulo tracker real, los mismos
 * ticks pasarán a mapear filas de patrón en vez de frames).
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
    // shortcut: ventanas más anchas que la web porque acá no hay pista de
    // audio todavía, solo el anillo visual — ajustar cuando entre la música.
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
    };

    constexpr score_tier PERFECT_TIER{ 300, 22, "EXCELLENT" };
    constexpr score_tier GREAT_TIER{ 200, 10, "GREAT" };
    constexpr score_tier GOOD_TIER{ 100, 0, "GOOD" };
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

    constexpr const char* DIFFICULTY_NAMES[] = { "FACIL", "NORMAL", "DIFICIL" };
    constexpr int DIFFICULTY_COUNT = sizeof(DIFFICULTY_NAMES) / sizeof(DIFFICULTY_NAMES[0]);

    constexpr const char* ENDED_OPTIONS[] = { "REINTENTAR", "MENU" };
    constexpr int ENDED_OPTION_COUNT = sizeof(ENDED_OPTIONS) / sizeof(ENDED_OPTIONS[0]);

    constexpr const char* MAIN_MENU_OPTIONS[] = { "JUGAR", "COMO JUGAR", "CREDITOS" };
    constexpr int MAIN_MENU_OPTION_COUNT = sizeof(MAIN_MENU_OPTIONS) / sizeof(MAIN_MENU_OPTIONS[0]);

    constexpr const char* HELP_LINES[] = {
        "PEGA CON LA CRUCETA",
        "AL PISAR EL ANILLO",
        "CADENA (FILA DE 3):",
        "MANTEN HASTA EL FIN",
        "EUFORIA LLENA: L+R",
        "PUNTOS DOBLES UN RATO",
        "",
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
    // 51 y no 50: al sacar HAYDEN (colgaba maxmod) cambió el layout de
    // high_scores — el magic nuevo descarta saves con el layout viejo.
    constexpr int SAVE_MAGIC = 0x474F4C51;

    struct save_data
    {
        int magic;
        int high_scores[SONG_LIST_COUNT][DIFFICULTY_COUNT];
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

    bool lane_held(lane ln)
    {
        switch (ln)
        {
        case lane::up: return bn::keypad::up_held();
        case lane::down: return bn::keypad::down_held();
        case lane::left: return bn::keypad::left_held();
        case lane::right: return bn::keypad::right_held();
        }
        return false;
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
        bool holding = false;
        bn::optional<bn::sprite_ptr> sprite;
        // Solo cadenas: eslabones repetidos detrás de la cabeza, que se
        // consumen mientras se sostiene — el "mantené presionado" se ve.
        bn::vector<bn::sprite_ptr, 2> trail;
    };

    void style_sprite(bn::sprite_ptr& sprite, enemy_type type, bool holding, bool euphoria_active)
    {
        bn::sprite_palette_ptr palette = sprite.palette();
        if (holding)
        {
            palette.set_fade(bn::colors::cyan, 0.7);
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
            }
        }
        bn::sram::write(save);
    }

    bn::vector<chart_note, CHART_MAX_NOTES> facil_chart;
    bn::vector<chart_note, CHART_MAX_NOTES> normal_chart;
    bn::vector<chart_note, CHART_MAX_NOTES> dificil_chart;
    build_chart(difficulty::facil, facil_chart);
    build_chart(difficulty::normal, normal_chart);
    build_chart(difficulty::dificil, dificil_chart);

    const auto chart_for_difficulty = [&](int diff_index) -> const bn::ivector<chart_note>&
    {
        if (diff_index == 0)
        {
            return facil_chart;
        }
        if (diff_index == 1)
        {
            return normal_chart;
        }
        return dificil_chart;
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
    // mismo diseño que golpe-a-tiempo.html — generada por
    // assets-src/make_arena.py. El exterior del piso es transparente, así
    // se sigue viendo el zone_color por canción que pone el loop principal.
    bn::regular_bg_ptr arena_bg = bn::regular_bg_items::arena.create_bg(0, 0);
    arena_bg.set_visible(false);

    // Tarjeta de menú (borde, sombra desplazada y barra de acento con los
    // colores de carril), como el .panel del HTML. Exterior transparente;
    // en la pantalla de fin se dibuja encima de la arena, como el panel
    // de resultados del HTML. z_order menor = delante, misma prioridad.
    bn::regular_bg_ptr panel_bg = bn::regular_bg_items::menu_panel.create_bg(0, 0);
    panel_bg.set_z_order(-1);

    bn::vector<bn::sprite_ptr, 20> title_line1;
    bn::vector<bn::sprite_ptr, 24> title_line2;

    const auto draw_title = [&]()
    {
        // -58 y no -66: justo debajo de la barra de acento de la tarjeta.
        text_generator.generate(-40, -58, "DEAD AIR", title_line1);
    };
    draw_title();

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
    bn::vector<bn::sprite_ptr, 36> message_text;
    bn::vector<bn::sprite_ptr, 16> judgement_text;
    int judgement_until = 0;

    // 12 y no 8: las cadenas ocupan su lugar durante todo el hold (hasta
    // ~6 beats entre acercarse y sostenerse) y en DIFICIL conviven con
    // una nota por beat — con 8 se llenaba y las notas aparecían tarde.
    bn::vector<active_note, 12> active_notes;

    // Animación de salida: al resolverse, el sprite no desaparece en seco —
    // vuela hacia afuera (derrotado) o se mete hasta el héroe (te pegó),
    // como en golpe-a-tiempo.html.
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

    const auto save_high_score = [&]()
    {
        int& record = save.high_scores[selected_song_index][selected_difficulty];
        if (score > record)
        {
            record = score;
            bn::sram::write(save);
            return true;
        }
        return false;
    };

    const auto resolve_hit = [&](active_note& note, const score_tier& tier)
    {
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
            else if (bn::keypad::start_pressed())
            {
                for (int k = 0; k < MAIN_MENU_OPTION_COUNT; ++k)
                {
                    main_menu_line[k].clear();
                }
                bn::sound_items::sfx_menu_select.play();

                if (main_menu_cursor == 0)
                {
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
                    else
                    {
                        draw_credits();
                        phase = game_phase::credits;
                    }
                }
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
                draw_title();
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
                draw_main_menu();
                phase = game_phase::main_menu;
                bn::sound_items::sfx_menu_select.play();
            }
            else if (bn::keypad::start_pressed())
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
            else if (bn::keypad::start_pressed())
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
            const bn::ivector<chart_note>& current_chart = chart_for_difficulty(selected_difficulty);
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
                const bn::sprite_item& enemy_item =
                        lane_sprite_item(note.ln, SONG_LIST[selected_song_index].enemy_set);
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

            int i = 0;
            while (i < active_notes.size())
            {
                active_note& note = active_notes[i];
                int diff_frames = note.hit_frame - frame;
                bool remove = false;

                if (note.type == enemy_type::chain)
                {
                    if (!note.holding)
                    {
                        if (-diff_frames > MISS_FRAMES)
                        {
                            detach_sprite(note, false);
                            register_miss(note);
                            remove = true;
                        }
                        else if (diff_frames <= MISS_FRAMES && lane_held(note.ln))
                        {
                            note.holding = true;
                        }
                    }
                    else
                    {
                        if (!lane_held(note.ln))
                        {
                            detach_sprite(note, false);
                            register_miss(note);
                            remove = true;
                        }
                        else if (frame >= note.hold_end_frame)
                        {
                            detach_sprite(note, true);
                            resolve_hit(note, GREAT_TIER);
                            remove = true;
                        }
                    }
                }
                else if (-diff_frames > MISS_FRAMES)
                {
                    detach_sprite(note, false);
                    register_miss(note);
                    remove = true;
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

                bn::fixed distance;
                if (note.holding)
                {
                    // Mientras se sostiene, la cadena se desliza del anillo
                    // hacia el héroe: se ve cuánto falta (llega = derrotada).
                    int hold_left = note.hold_end_frame - frame;
                    int hold_total = note.hold_end_frame - note.hit_frame;
                    distance = 12 + (HIT_DISTANCE - 12) * hold_left / hold_total;
                }
                else
                {
                    distance = HIT_DISTANCE + progress * (lane_spawn_distance(note.ln) - HIT_DISTANCE);
                }
                note.sprite->set_position(lane_direction(note.ln) * distance);
                style_sprite(*note.sprite, note.type, note.holding, euphoria_active);

                for (int t = 0; t < note.trail.size(); ++t)
                {
                    bn::sprite_ptr& link = note.trail[t];
                    link.set_position(lane_direction(note.ln) * (distance + 14 * (t + 1)));
                    bool link_alive = true;
                    if (note.holding)
                    {
                        // Los eslabones se consumen con el hold: quedan 2,
                        // después 1, después solo la cabeza (ya casi está).
                        int hold_left = note.hold_end_frame - frame;
                        int hold_total = note.hold_end_frame - note.hit_frame;
                        link_alive = hold_left * 3 > hold_total * (t + 1);
                    }
                    link.set_visible(link_alive);
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
                    if (note.ln != ln || note.type == enemy_type::chain)
                    {
                        continue;
                    }

                    int diff = note.hit_frame - frame;
                    int abs_diff = diff < 0 ? -diff : diff;
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
                    detach_sprite(note, true);
                    resolve_hit(note, tier);
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
                bool new_record = save_high_score();
                message_text.clear();
                text_generator.generate(-48, -16, "GAME OVER", message_text);
                if (new_record)
                {
                    text_generator.generate(-56, 2, "NUEVO RECORD", message_text);
                }
                ended_cursor = 0;
                draw_ended_menu();
            }
            else if (next_chart_index >= current_chart.size() && active_notes.empty())
            {
                phase = game_phase::ended;
                bn::music::stop();
                bool new_record = save_high_score();
                message_text.clear();
                text_generator.generate(-24, -16, "LISTO", message_text);
                int message_y = 2;
                if (miss_count == 0)
                {
                    text_generator.generate(-48, message_y, "FULL COMBO", message_text);
                    message_y += 16;
                }
                if (new_record)
                {
                    text_generator.generate(-56, message_y, "NUEVO RECORD", message_text);
                }
                ended_cursor = 0;
                draw_ended_menu();
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
            // El fondo oscuro del HTML (#19132d) detrás de la tarjeta.
            bn::bg_palettes::set_transparent_color(bn::color(3, 2, 5));
        }

        // La arena solo tiene sentido jugando (y de fondo en la pantalla de
        // fin); en los menús se esconde y queda el color plano de siempre.
        arena_bg.set_visible(phase == game_phase::playing || phase == game_phase::ended);
        panel_bg.set_visible(phase != game_phase::playing);

        // Temblor de la arena al fallar, como el shake del canvas del HTML.
        if (phase == game_phase::playing && frame < shake_until)
        {
            arena_bg.set_position(frame % 2 == 0 ? 2 : -2, frame % 2 == 0 ? -1 : 1);
        }
        else
        {
            arena_bg.set_position(0, 0);
        }

        // Pulso de beat: el escenario se ilumina en cada bombo y decae, como
        // el anillo del HTML; la euforia lo deja con brillo extra sostenido.
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
            hero_sprite.set_position(phase == game_phase::playing ? bn::fixed_point(0, 0) : bn::fixed_point(88, 14));
            hero_sprite.set_item(*hero_item, 0);
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

        if (phase == game_phase::playing || phase == game_phase::ended)
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

            for (int i = 0; i < MAX_HP; ++i)
            {
                bn::sprite_ptr& heart_sprite = heart_sprites[i];
                heart_sprite.set_visible(true);
                bn::sprite_palette_ptr heart_palette = heart_sprite.palette();
                if (i < hp)
                {
                    heart_palette.set_fade_intensity(0);
                }
                else
                {
                    heart_palette.set_fade(bn::colors::gray, 0.75);
                }
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

        ++frame;
        bn::core::update();
    }
}
