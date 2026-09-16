#pragma once
#include "Core/Types.hpp"
#include "SynthEngine.hpp"   // WaveKind, kEnvLut, kEnvWindow

// Инструмент — ЗВУК ноты: форма сигнала + огибающая (кривая уровня по
// времени). Больше ничего: синтез фиксирован (осциллятор + LUT-огибающая),
// и инструмент меняет только эти две величины.
//
// Огибающая — таблица из до 32 точек (t — секунды в окне 0..kEnvWindow,
// v — уровень 0..1). Движок строит из неё свой LUT (BuildLut).

namespace Smp {

struct Instrument {
    char  name[48];
    u32   wave;                  // SynthEngine::WaveKind
    f32   envT[32];              // время точки, с (0..SynthEngine::kEnvWindow)
    f32   envV[32];              // уровень (0..1)
    s32   envN;                  // сколько точек занято (1..32)

    // Кривая в LUT движка (SynthEngine::kEnvLut значений): кусочно-линейно.
    void BuildLut(f32* out /* >= SynthEngine::kEnvLut */) const;
};

// Общая сборка LUT из n точек (t — с в окне, v — 0..1) в kEnvLut значений.
// До первой точки — её уровень, после последней — последний: кривая не
// обязана начинаться с нуля и заканчиваться на нуле (нота может и держать).
void BuildLut(const f32* t, const f32* v, s32 n, f32* out);

// Библиотека инструментов. Пока ОДИН звук — «Генератор синусоид»; форма
// сигнала у него переключается в настройках инструмента (HID-список),
// т.е. библиотека растёт точкой, а не кодом.
class InstrumentLib {
public:
    static s32 Count();
    static const Instrument& At(s32 i);
};

}  // namespace Smp
