/*
 * Charts generados por semilla (PRNG mulberry32),
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
    // Corazón curativo: se golpea como un enemigo y cura 1 HP; fallarlo
    // no castiga. Solo aparece de NORMAL hacia arriba.
    heart,
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
    // Progresión por carriles: FACIL un solo carril (abajo), NORMAL los
    // dos laterales, DIFICIL tres, MUY DIFICIL los cuatro.
    facil,
    normal,
    dificil,
    muy_dificil,
    // Chart especial de la canción BOSS (no aparece en el menú): una nota
    // por beat y ráfagas muy seguidas, cuatro carriles.
    boss,
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

private:
    unsigned _state;
};

inline void build_chart(difficulty diff, bn::ivector<chart_note>& out)
{
    // Carriles habilitados por dificultad: la progresión suma un carril
    // por escalón, así alguien nuevo aprende el timing en uno solo antes
    // de preocuparse por las direcciones.
    constexpr lane FACIL_LANES[] = { lane::down };
    constexpr lane NORMAL_LANES[] = { lane::left, lane::right };
    constexpr lane DIFICIL_LANES[] = { lane::left, lane::right, lane::down };
    constexpr lane ALL_LANES[] = { lane::up, lane::down, lane::left, lane::right };

    const lane* lanes = ALL_LANES;
    int lane_count = 4;
    if (diff == difficulty::facil)
    {
        lanes = FACIL_LANES;
        lane_count = 1;
    }
    else if (diff == difficulty::normal)
    {
        lanes = NORMAL_LANES;
        lane_count = 2;
    }
    else if (diff == difficulty::dificil)
    {
        lanes = DIFICIL_LANES;
        lane_count = 3;
    }

    unsigned seed = diff == difficulty::facil ? 7u
            : diff == difficulty::normal ? 17u
            : diff == difficulty::dificil ? 29u
            : diff == difficulty::muy_dificil ? 37u : 43u;
    chart_rng rng(seed);

    // Ráfagas (fila de 3, estilo Muse Dash): cada tanto viene una fila
    // que se revienta tocando la cruceta 3 veces; hold_beats es la
    // ventana de mash una vez que llega al anillo. Más seguidas a mayor
    // dificultad. La primera recién pasado el beat 16, para que la
    // canción arranque con golpes simples.
    int chain_period = diff == difficulty::facil ? 16
            : diff == difficulty::normal ? 12
            : diff == difficulty::dificil ? 10
            : diff == difficulty::muy_dificil ? 8 : 6;
    int next_chain_beat = 16;

    // Un corazón curativo aproximadamente a la mitad, de NORMAL hacia
    // arriba (en FACIL casi no se pierde vida, sería ruido).
    bool heart_pending = diff != difficulty::facil;

    int last_lane = -1;
    int beat = 0;
    int toggle = 0;
    while (beat < CHART_LENGTH_BEATS && !out.full())
    {
        lane ln;
        do
        {
            ln = lanes[rng.next() % unsigned(lane_count)];
        }
        // Con varios carriles evita repetir el mismo; con los 4 evita
        // también el opuesto (arriba<->abajo, izquierda<->derecha): en
        // una cruceta real pasar al opuesto es el peor caso posible.
        while (lane_count > 1 && (int(ln) == last_lane ||
                (lane_count == 4 && int(ln) == (last_lane ^ 1))));
        last_lane = int(ln);

        if (beat >= next_chain_beat)
        {
            int hold_beats = diff == difficulty::facil ? 2 : 2 + int(rng.next() % 2u);
            out.push_back({ beat, ln, enemy_type::chain, hold_beats });
            // Un beat de respiro tras la ráfaga, y el espaciado normal no aplica.
            beat += hold_beats + 1;
            next_chain_beat = beat + chain_period;
            continue;
        }

        enemy_type type = enemy_type::basic;
        if (heart_pending && beat >= CHART_LENGTH_BEATS / 2)
        {
            type = enemy_type::heart;
            heart_pending = false;
        }
        out.push_back({ beat, ln, type, 0 });

        if (diff == difficulty::facil)
        {
            beat += 2;
        }
        else if (diff == difficulty::normal)
        {
            // Normal: alterna 1 y 2 beats de separación.
            beat += (toggle % 2 == 0) ? 1 : 2;
            ++toggle;
        }
        else
        {
            beat += 1;
        }
    }
}

#endif
