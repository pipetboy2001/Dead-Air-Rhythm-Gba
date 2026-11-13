/*
 * Charts generados por semilla (mismo PRNG mulberry32 que la versión web),
 * uno por dificultad — la canción es un eje aparte (ver SONG_LIST en
 * main.cpp). Todos con enemy_type::basic nada más ("solo cruceta"). El
 * código de acorazado/cadena/proyectil/euforia sigue en main.cpp sin
 * usarse, por si se retoma más adelante.
 *
 * beat * FRAMES_PER_BEAT = frame del juicio (ver main.cpp).
 */
#ifndef CHART_H
#define CHART_H

#include "bn_vector.h"

enum class enemy_type
{
    basic,
    armored,
    chain,
    projectile,
};

enum class lane
{
    up,
    down,
    left,
    right,
};

struct chart_note
{
    int beat;
    lane ln;
    enemy_type type;
    int hold_beats;
};

enum class difficulty
{
    facil,
    normal,
    dificil,
};

// ~120 beats a 29 frames/beat ~= 58s, cerca de 1 minuto por canción.
constexpr int CHART_LENGTH_BEATS = 120;
constexpr int CHART_MAX_NOTES = 130;

class chart_rng
{
public:
    explicit chart_rng(unsigned seed) :
        _state(seed)
    {
    }

    unsigned next()
    {
        _state += 0x6D2B79F5u;
        unsigned t = _state;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return t ^ (t >> 14);
    }

    int next_lane_index()
    {
        return int(next() % 4u);
    }

private:
    unsigned _state;
};

inline void build_chart(difficulty diff, bn::ivector<chart_note>& out)
{
    constexpr lane LANES[] = { lane::up, lane::down, lane::left, lane::right };
    unsigned seed = diff == difficulty::facil ? 7u : diff == difficulty::normal ? 17u : 29u;
    chart_rng rng(seed);

    // Ráfagas (fila de 3, estilo Muse Dash): cada tanto viene una fila
    // que se revienta tocando la cruceta 3 veces; hold_beats es la
    // ventana de mash una vez que llega al anillo. Más seguidas a mayor
    // dificultad. La primera recién pasado el beat 16, para que la
    // canción arranque con golpes simples.
    int chain_period = diff == difficulty::facil ? 16 : diff == difficulty::normal ? 12 : 8;
    int next_chain_beat = 16;

    int last_lane = -1;
    int beat = 0;
    int toggle = 0;
    while (beat < CHART_LENGTH_BEATS && !out.full())
    {
        int lane_index;
        do
        {
            lane_index = rng.next_lane_index();
        }
        // Evita repetir el carril Y evita el carril opuesto (arriba<->abajo,
        // izquierda<->derecha): en una cruceta real, pasar de un lado al
        // opuesto es el peor caso posible, no solo más difícil.
        while (lane_index == last_lane || lane_index == (last_lane ^ 1));
        last_lane = lane_index;

        if (beat >= next_chain_beat)
        {
            int hold_beats = diff == difficulty::facil ? 2 : 2 + int(rng.next() % 2u);
            out.push_back({ beat, LANES[lane_index], enemy_type::chain, hold_beats });
            // Un beat de respiro tras la ráfaga, y el espaciado normal no aplica.
            beat += hold_beats + 1;
            next_chain_beat = beat + chain_period;
            continue;
        }

        out.push_back({ beat, LANES[lane_index], enemy_type::basic, 0 });

        if (diff == difficulty::facil)
        {
            beat += 2;
        }
        else if (diff == difficulty::dificil)
        {
            beat += 1;
        }
        else
        {
            // Normal: alterna 1 y 2 beats de separación.
            beat += (toggle % 2 == 0) ? 1 : 2;
            ++toggle;
        }
    }
}

#endif
