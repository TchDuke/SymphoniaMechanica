#include "SynthEngine.hpp"
#include "Instrument.hpp"   // общая сборка LUT и заводской инструмент
#include <cmath>

namespace Smp {

const f32 SynthEngine::kEnvWindow = 2.0f;
const f32 SynthEngine::kLimKnee = 0.85f;
// Табличка синуса заполняется в Init (один раз, до запуска звукового потока).
f32 SynthEngine::kSineTab[2048] = {0};

const char* SynthEngine::WaveName(WaveKind w) {
    switch(w) {
        case WaveSaw:      return "Пила";
        case WaveSquare:   return "Прямоугольник";
        case WaveTriangle: return "Треугольник";
        case WaveSine:     return "Синус";
        case WaveCount:    break;
    }
    return "Неизвестная волна";
}

void SynthEngine::Init(s32 sampleRate) {
    m_rate = sampleRate > 0 ? sampleRate : 48000;
    // Огибающая по умолчанию — кривая заводского инструмента: одна кривая в
    // одной точке (InstrumentLib::At(0)), а не вторая копия констант.
    {
        const Instrument& def = InstrumentLib::At(0);
        BuildLut(def.envT, def.envV, def.envN, m_env[0]);
    }
    m_envSel.store(0, std::memory_order_relaxed);
    m_envStep = 1.0f / ((f32)m_rate * kEnvWindow);
    // Хвост после NoteOff 25 мс: слышно, что нота легла, но не ползёт.
    m_relRate = 1.0f / ((f32)m_rate * 0.025f);
    for(s32 i = 0; i < 2048; ++i)
        kSineTab[i] = (f32)std::sin(2.0 * M_PI * (f32)i / 2048.0);
    for(s32 i = 0; i < kVoices; ++i)
        m_v[i].active = false;
    for(s32 i = 0; i < 128; ++i)
        m_chanPhase[i] = 0.0f;
}

SynthEngine::Voice* SynthEngine::findNote(s32 midi) {
    for(s32 i = 0; i < kVoices; ++i)
        if(m_v[i].active && m_v[i].midi == midi) return &m_v[i];
    return nullptr;
}

void SynthEngine::NoteOn(s32 midi, s32 velocity, WaveKind wave) {
    if(m_rate <= 0 || midi < 0 || midi > 127) return;
    if(wave >= WaveCount) wave = WaveSaw;

    // RETRIGGER: та же нота уже звучит — берём ЕЁ голос (огибающая с нуля,
    // фаза — с канала), а голос не плодится.
    Voice* found = findNote(midi);
    Voice* v = found;
    if(!v) {
        for(s32 i = 0; i < kVoices; ++i)
            if(!m_v[i].active) { v = &m_v[i]; break; }
        if(!v) {
            // ПОЛ ПРОЛЕТ: вытесняем САМОГО ПУРЕГО (больше всего age).
            // Не «случайного» и не «сверху ротации»: ротация вытесняет по кругу
            // все подряд, и под аккордом тонет то, о чём человек не знал.
            v = &m_v[0];
            for(s32 i = 1; i < kVoices; ++i)
                if(m_v[i].age > v->age) v = &m_v[i];
        }
    }
    v->active = true;
    v->midi = midi;
    v->wave = wave;
    // Когерентность: фаза — с того, где КАНАЛ её оставил. Осциллятор не
    // рестартуется от разрыва ноты; перебивки одной высоты «собирают» друг
    // друга, а не гремят с нуля.
    v->phase = m_chanPhase[midi];
    // f = 440 · 2^((n−69)/12), фаза на отсчёт = f/частота.
    v->phaseInc = (f32)(440.0 * std::pow(2.0, (f64)(midi - 69) / 12.0)) / (f32)m_rate;
    v->vel = (f32)velocity / 127.0f;
    if(v->vel < 0.05f) v->vel = 0.05f;
    v->envPos = 0.0f;
    // РЕТРИГГЕР: старый уровень НЕ обнуляем. Звучавший голос скачком с
    // 15329 до 0 за ОДИН отсчёт — это тот самый «щелчок прерывистого»
    // (замер 16.09.2026: retrigger удерживаемой ноты = 1 импульс). Свой
    // уровень голос стухнет сам — в Render он сползает по хвосту, пока
    // атака растёт. Свежий голос — с нуля.
    if(!found) v->level = 0.0f;
    v->releasing = false;
    v->age = 0;
}

void SynthEngine::NoteOff(s32 midi) {
    Voice* v = findNote(midi);
    if(v) v->releasing = true;
}

void SynthEngine::SetEnvelope(const f32* lut) {
    // Кладём в другую половину (пинг-понг): звуковой поток в любой момент
    // читает цельную таблицу, запись никогда не пересекается с чтением.
    const u32 sel = m_envSel.load(std::memory_order_relaxed);
    f32* dst = m_env[sel ^ 1u];
    for(s32 i = 0; i < kEnvLut; ++i)
        dst[i] = lut[i];
    m_envSel.store(sel ^ 1u, std::memory_order_release);
}

Real SynthEngine::osc(f32 phase, u32 wave) const {
    switch(wave) {
        case WaveSaw:      return (f32)(2.0 * phase - 1.0);
        case WaveSquare:   return phase < 0.5f ? 0.5f : -0.5f;
        case WaveTriangle: return phase < 0.5f ? (f32)(4.0 * phase - 1.0) : (f32)(3.0 - 4.0 * phase);
        case WaveSine: {
            // f32-фаза — 24 бит: в таблицу на 2048 (11 бит) это точный адрес
            // с запасом, интерполяция не нужна (12 бит лишней точности зря).
            s32 i = (s32)(phase * 2048.0f) & 0x7FF;
            return kSineTab[i];
        }
    }
    return 0.0f;
}

void SynthEngine::Render(Real* out, s32 frames, Real master) {
    for(s32 i = 0; i < frames; ++i) out[i] = 0.0f;
    if(frames <= 0 || m_rate <= 0) return;
    const f32* lut = m_env[m_envSel.load(std::memory_order_acquire)];

    for(s32 i = 0; i < kVoices; ++i) {
        Voice& v = m_v[i];
        if(!v.active) continue;
        ++v.age;
        for(s32 n = 0; n < frames; ++n) {
            if(v.releasing) {
                // Хвост: уровень уходит с текущего (LUT-ного) значения к нулю.
                v.level -= m_relRate;
                if(v.level <= 0.0f) { v.level = 0.0f; v.active = false; break; }
            } else {
                v.envPos += m_envStep;
                if(v.envPos >= 1.0f) v.envPos = 1.0f;   // после окна — удержание
                s32 idx = (s32)(v.envPos * (f32)kEnvLut);
                if(idx >= kEnvLut) idx = kEnvLut - 1;
                // УРОВЕНЬ НИКОГДА НЕ ПРЫГает: атака ползёт по кривой, а старый
                // (ретриггернутый) уровень стухает своим хвостом — берём, кто
                // выше. Новый голос стартует с нуля — поведение не меняется.
                if(v.level > lut[idx]) v.level -= m_relRate;
                else if(v.level < lut[idx]) v.level = lut[idx];
            }
            out[n] += v.level * v.vel * osc(v.phase, v.wave);
            v.phase += v.phaseInc;
            if(v.phase >= 1.0f) v.phase -= 1.0f;
        }
        // Фаза канала переживает голос: записываем её, пока голос жив, —
        // на этом месте следующая нота той же высоты продолжит её.
        if(v.active) m_chanPhase[v.midi] = v.phase;
        else v.age = 0;   // освободившийся голос — не «древний»
    }

    // Ограничитель по сумме голосов: при полифонии сумма превышает единицу
    // по построению. ЖЁСТКИЙ клиппинг здесь — те самые «искажения» на аккорде
    // (замер 16.09.2026: 5 голосов пилой = 1688 плоских участков). Поэтому —
    // мягкое колено: ниже kLimKnee единичный пропуск (одиночная нота не
    // тронута), выше — tanh-сгиб, непрерывный по значению и наклону,
    // уходит к потолку, но его не касается.
    for(s32 i = 0; i < frames; ++i) {
        Real s = out[i] * master;
        const Real a = (s < 0.0f) ? -s : s;
        if(a > kLimKnee) {
            const Real over = (a - kLimKnee) / (1.0f - kLimKnee);
            const Real r = kLimKnee + (1.0f - kLimKnee) * (Real)std::tanh((double)over);
            s = (s < 0.0f) ? -r : r;
        }
        out[i] = s;
    }
}

s32 SynthEngine::ActiveVoices() const {
    s32 c = 0;
    for(s32 i = 0; i < kVoices; ++i)
        if(m_v[i].active) ++c;
    return c;
}

void SynthEngine::ActiveNotes(s32* out) const {
    for(s32 i = 0; i < kVoices; ++i)
        if(m_v[i].active) out[i] = m_v[i].midi;
    out[kVoices] = -1;
}

}  // namespace Smp
