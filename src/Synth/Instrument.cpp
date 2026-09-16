#include "Instrument.hpp"

#include <cstdio>
#include <cstring>

namespace Smp {

void BuildLut(const f32* t, const f32* v, s32 n, f32* out) {
    if(n < 1) n = 1;
    const s32 cap = SynthEngine::kEnvLut;
    const f32 win = (f32)SynthEngine::kEnvWindow;
    for(s32 j = 0; j < cap; ++j) {
        // Центр ступени: ступень j покрывает [j, j+1)-ю долю окна.
        const f32 x = (f32)(j + 0.5f) * win / (f32)cap;
        f32 y;
        if(n <= 1 || x <= t[0])
            y = v[0];
        else if(x >= t[n - 1])
            y = v[n - 1];
        else {
            s32 i = 0;
            while(i + 1 < n && t[i + 1] < x) ++i;   // n ≤ 32: линейный ход — дёшево
            const f32 a = t[i], b = t[i + 1];
            // Время дробится, если точки стоят на одном t — берём левую.
            y = (b > a) ? v[i] + (v[i + 1] - v[i]) * (x - a) / (b - a)
                        : v[i];
        }
        out[j] = (y < 0.0f) ? 0.0f : ((y > 1.0f) ? 1.0f : y);
    }
}

void Instrument::BuildLut(f32* out) const {
    Smp::BuildLut(envT, envV, envN, out);   // с namespace: член прячет свободный
}

// «Генератор синусоид» — первая и пока единственная запись библиотеки.
// Кривая: короткая атака (~0.1 с), первая секунда — на единице, потом спад
// к 0.30: удержанная нота тихо гудит, а не давит, и под ней слышно соседей.
static const f32 kDefT[5] = { 0.00f, 0.02f, 0.12f, 1.00f, 2.00f };   // с
static const f32 kDefV[5] = { 0.00f, 0.00f, 1.00f, 1.00f, 0.30f };   // 0..1

s32 InstrumentLib::Count() {
    return 1;
}

const Instrument& InstrumentLib::At(s32 i) {
    (void)i;   // пока одна запись; номер оставлен под рост библиотеки
    static Instrument inst;
    static bool inited = false;
    if(!inited) {
        std::snprintf(inst.name, sizeof inst.name, "%s", "Генератор синусоид");
        inst.wave = (u32)SynthEngine::WaveSine;
        for(s32 k = 0; k < 5; ++k) {
            inst.envT[k] = kDefT[k];
            inst.envV[k] = kDefV[k];
        }
        inst.envN = 5;
        inited = true;
    }
    return inst;
}

}  // namespace Smp
