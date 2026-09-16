#include "Timeline.hpp"

#include <cmath>
#include <cstring>

namespace Smp {

bool Timeline::Insert(s32 row, s32 beat, s32 midi, u32 wave, bool staccato) {
    if(row < 0) row = 0;
    if(row >= kRows) row = kRows - 1;
    if(beat < 0) beat = 0;
    if(beat >= kBeats) beat = kBeats - 1;
    if(midi < 0 || midi > 127) return false;

    std::lock_guard<std::mutex> lk(m_mu);
    if(coverLocked(row, beat, midi) >= 0) return false;   // клетка в чужом штрихе
    const s32 slot = findLocked(row, beat, midi);
    if(slot >= 0) {
        m_note[slot].wave = wave;    // уже стоит: перекрасить инструментом
        m_note[slot].dur = 1;
        m_note[slot].staccato = staccato;
        return true;
    }
    if(m_count >= kMaxNote) return false;
    Note& n = m_note[m_count++];
    n.used = true;
    n.row = row;
    n.beat = beat;
    n.midi = midi;
    n.wave = wave;
    n.dur = 1;
    n.staccato = staccato;
    return true;
}

// Индекс ноты в (дорожке, доле, ноте) под ЗАНЯТЫМ замком; -1 — нет такой.
// Insert/Remove/Has — один путь в одни данные, не три похожих цикла.
s32 Timeline::findLocked(s32 row, s32 beat, s32 midi) const {
    for(s32 i = 0; i < m_count; ++i) {
        const Note& n = m_note[i];
        if(n.row == row && n.beat == beat && n.midi == midi) return i;
    }
    return -1;
}

// Тот же обход, но доля — ВНУТРИ длительности ноты (штрих покрывает её).
s32 Timeline::coverLocked(s32 row, s32 beat, s32 midi) const {
    for(s32 i = 0; i < m_count; ++i) {
        const Note& n = m_note[i];
        if(n.row == row && n.midi == midi &&
           beat >= n.beat && beat < n.beat + n.dur)
            return i;
    }
    return -1;
}

bool Timeline::HasAny(s32 row, s32 beat, s32 midi) const {
    std::lock_guard<std::mutex> lk(m_mu);
    return coverLocked(row, beat, midi) >= 0;
}

bool Timeline::Paint(s32 row, s32 lo, s32 hi, s32 midi, u32 wave) {
    if(row < 0) row = 0;
    if(row >= kRows) row = kRows - 1;
    if(lo < 0) lo = 0;
    if(hi >= kBeats) hi = kBeats - 1;
    if(hi < lo) { s32 t = lo; lo = hi; hi = t; }
    if(midi < 0 || midi > 127) return false;

    std::lock_guard<std::mutex> lk(m_mu);
    const s32 hiEnd = hi + 1;   // правый край отрезка (не включённый)

    // Предперегон: сколько слотов прибавится. Штрих — +1 всегда; единственный
    // +2 — нота, перерезающая ОБЕ границы (левая часть остаётся, хвост
    // за правой границей — новая нота). Всё остальное либо удаляется, либо
    // укорачивается/сдвигается на месте.
    s32 del = 0, span = 0;
    for(s32 i = 0; i < m_count; ++i) {
        const Note& n = m_note[i];
        if(n.row != row || n.midi != midi) continue;
        if(!(n.beat + n.dur > lo && n.beat < hiEnd)) continue;
        if(n.beat < lo) {
            if(n.beat + n.dur > hiEnd) span = 1;
        }
        else if(n.beat + n.dur <= hiEnd) ++del;
    }
    if(m_count - del + 1 + span > kMaxNote) return false;   // не будет: нот не больше клеток

    bool changed = false;
    Note tail = Note{};
    bool haveTail = false;

    s32 i = 0;
    while(i < m_count) {
        Note& n = m_note[i];
        if(n.row == row && n.midi == midi &&
           n.beat + n.dur > lo && n.beat < hiEnd) {
            changed = true;
            const s32 end = n.beat + n.dur;
            if(n.beat < lo) {
                // Левая часть [beat..lo-1] за границей — живёт, только
                // укорачивается…
                n.dur = lo - n.beat;
                if(end > hiEnd) {
                    // …а хвост за hi выносится отдельной нотой.
                    haveTail = true;
                    tail = Note{};
                    tail.used = true;
                    tail.row = row;
                    tail.midi = midi;
                    tail.wave = n.wave;
                    tail.beat = hiEnd;
                    tail.dur = end - hiEnd;
                }
                ++i;
            }
            else if(end > hiEnd) {
                // Начинается в диапазоне и торчит за правый край: сдвигается
                // целиком за край.
                n.beat = hiEnd;
                n.dur = end - hiEnd;
                ++i;
            }
            else {
                // Целиком внутри: на её месте штрих.
                for(s32 j = i; j + 1 < m_count; ++j) m_note[j] = m_note[j + 1];
                --m_count;
            }
        }
        else ++i;
    }

    Note& s = m_note[m_count++];
    s = Note{};
    s.used = true;
    s.row = row;
    s.beat = lo;
    s.midi = midi;
    s.wave = wave;
    s.dur = hi - lo + 1;
    if(haveTail) m_note[m_count++] = tail;
    return changed;
}

bool Timeline::RemoveCell(s32 row, s32 beat, s32 midi) {
    if(row < 0 || row >= kRows) return false;
    if(beat < 0 || beat >= kBeats) return false;
    if(midi < 0 || midi > 127) return false;
    std::lock_guard<std::mutex> lk(m_mu);
    for(s32 i = 0; i < m_count; ++i) {
        Note& n = m_note[i];
        if(n.row != row || n.midi != midi) continue;
        if(beat < n.beat || beat >= n.beat + n.dur) continue;
        if(n.dur <= 1) {
            for(s32 j = i; j + 1 < m_count; ++j) m_note[j] = m_note[j + 1];
            --m_count;
            return true;
        }
        if(beat == n.beat) {
            // Срезал начало: нота сдвинулась на долю вправо.
            n.beat = beat + 1;
            --n.dur;
            return true;
        }
        if(m_count >= kMaxNote) return false;   // сетка битком одиночными
        Note& r = m_note[m_count++];
        r = n;
        r.beat = beat + 1;                          // правая половина
        r.dur = (n.beat + n.dur) - (beat + 1);
        n.dur = beat - n.beat;                      // левая
        return true;
    }
    return false;
}

bool Timeline::Remove(s32 row, s32 beat, s32 midi) {
    std::lock_guard<std::mutex> lk(m_mu);
    const s32 slot = findLocked(row, beat, midi);
    if(slot < 0) return false;
    // Сдвинул хвостом: список остаётся без дыр, порядок — как на ленте.
    for(s32 i = slot; i + 1 < m_count; ++i) m_note[i] = m_note[i + 1];
    --m_count;
    return true;
}

bool Timeline::Has(s32 row, s32 beat, s32 midi) const {
    std::lock_guard<std::mutex> lk(m_mu);
    return findLocked(row, beat, midi) >= 0;
}

void Timeline::Clear() {
    std::lock_guard<std::mutex> lk(m_mu);
    for(Note& n : m_note) n = Note{};   // валидацией: Note — не тривиальный
    m_count = 0;
}

s32 Timeline::NoteCount() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_count;
}

s32 Timeline::CopyNotes(Note* dst, s32 maxNotes) const {
    std::lock_guard<std::mutex> lk(m_mu);
    if(maxNotes < 0) maxNotes = 0;
    if(maxNotes > m_count) maxNotes = m_count;
    for(s32 i = 0; i < maxNotes; ++i) dst[i] = m_note[i];
    return maxNotes;
}

void Timeline::Play() {
    std::lock_guard<std::mutex> lk(m_mu);
    if(m_playing) return;
    m_playing = true;
    m_stopPending = false;
    m_pos = 0.0f;               // пуск — всегда с начала окна
}

void Timeline::Stop() {
    std::lock_guard<std::mutex> lk(m_mu);
    if(!m_playing) return;
    m_stopPending = true;       // доесть выстрелы «выкл» — звуковому потоку
}

bool Timeline::Playing() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_playing;
}

f32 Timeline::PosBeats() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_pos;
}

void Timeline::SetBpm(f32 bpm) {
    if(bpm < 30.0f) bpm = 30.0f;
    if(bpm > 240.0f) bpm = 240.0f;
    std::lock_guard<std::mutex> lk(m_mu);
    m_bpm = bpm;
}

f32 Timeline::Bpm() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_bpm;
}

void Timeline::SetMetro(bool on) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_metro = on;
}

bool Timeline::Metro() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_metro;
}

s32 Timeline::Step(f32 dtBeats, Fired* out, s32 maxOut) {
    std::lock_guard<std::mutex> lk(m_mu);
    if(!m_playing) {
        m_stopPending = false;  // стоп без пуска — просто сброшен
        return 0;
    }
    return stepLocked(dtBeats, out, maxOut);
}

s32 Timeline::stepLocked(f32 dtBeats, Fired* out, s32 maxOut) {
    s32 n = 0;
    const auto emit = [&](Fired f) { if(n < maxOut) out[n++] = f; };

    if(m_stopPending) {
        // Плавный стоп: гасим то, что звучит прямо сейчас, и замираем.
        // «Выкл» — ОДИН раз на высоту: движок держит по голосу на высоту,
        // а выстрелов за стоп может быть до всей сетки (2304) — больше,
        // чем влезает в батч; без дедупликации затесавшаяся нота осталась бы
        // висеть (NoteOff не дошёл).
        s32 done[128];
        s32 dn = 0;
        for(s32 i = 0; i < m_count; ++i) {
            const Note& e = m_note[i];
            if(!(e.beat <= m_pos && m_pos < e.beat + e.dur)) continue;
            bool dup = false;
            for(s32 j = 0; j < dn && !dup; ++j) dup = (done[j] == e.midi);
            if(dup) continue;
            if(dn < 128) done[dn++] = e.midi;
            emit({ 1, e.midi, e.wave, 0, 0 });
        }
        m_playing = false;
        m_stopPending = false;
        m_pos = 0.0f;
        return n;
    }

    const f32 prev = m_pos;
    m_pos += dtBeats;
    const f32 now = m_pos;

    // Занятость клеток: 0 — пусто, 1 — обычная клетка, 2 — стаккато-клетка.
    // Обычные клетки подряд — ОДИН непрерывный звук (legato): «вкл»
    // только в начале ряда, «выкл» только в конце. Без этого на каждой
    // границе клеток ретриггер удерживаемой ноты — просадка уровня за
    // ~25 мс, а это тот самый «прерывистый» звук. Стаккато (Ctrl)
    // клетка прерывает ряд в обе стороны: сама короткая фраза со своей
    // паузой, соседи начинают и заканчивают голос по-своему.
    char occ[kRows][kBeats];
    std::memset(occ, 0, sizeof occ);
    for(s32 i = 0; i < m_count; ++i) {
        const Note& e = m_note[i];
        occ[e.row][e.beat] = e.staccato ? 2 : 1;
    }

    // Выстрелы нот собираем батчем и сортируем ПО ДОЛЯМ (на одной доле —
    // «выкл» раньше «вкл»): звуковой поток исполняет всё за один чанк, и
    // порядок на границе решает звук — «выкл» первым, тогда отпускание и
    // новая нота, «вкл» первым, тогда удержание рвётся. Сортировка снимает
    // зависимость от порядка ВСТАВКИ (Ctrl-клетка ставится в любой миг).
    // Чанк (42,7 мс) короче любой доли (240 BPM = 250 мс) — в нём одна
    // целая граница, выстрелов ≤ 36 «вкл» + 36 «выкл», батч 128 с запасом.
    Fired nv[128];
    s32 nn = 0;
    for(s32 i = 0; i < m_count; ++i) {
        const Note& e = m_note[i];
        const s32 b0 = e.beat;
        const s32 b1 = b0 + e.dur;
        if(e.staccato) {
            // Своя фраза: «вкл» в долю + «выкл» спустя kStaccatoChunks чанков
            // (≈128 мс) — остаток доли тишина. Слитности с соседями не несёт.
            if(b0 > prev && b0 <= now && nn + 2 <= (s32)(sizeof nv / sizeof nv[0])) {
                nv[nn++] = { 0, e.midi, e.wave, b0, 0 };
                nv[nn++] = { 1, e.midi, e.wave, b0, kStaccatoChunks };
            }
            continue;
        }
        if(b0 > prev && b0 <= now && !(b0 > 0 && occ[e.row][b0 - 1] == 1))
            if(nn < (s32)(sizeof nv / sizeof nv[0]))
                nv[nn++] = { 0, e.midi, e.wave, b0, 0 };
        if(b1 > prev && b1 <= now && !(b1 < kBeats && occ[e.row][b1] == 1))
            if(nn < (s32)(sizeof nv / sizeof nv[0]))
                nv[nn++] = { 1, e.midi, e.wave, b1, 0 };
    }
    // Вставка-сортировка (элементов ≤ 128, шаг 24 р/с): ключ — доля, потом
    // ранг (выкл=0 раньше вкл=1).
    for(s32 i = 1; i < nn; ++i) {
        const Fired cur = nv[i];
        const s32 curRank = (cur.kind == 1) ? 0 : 1;
        s32 j = i - 1;
        while(j >= 0 && (nv[j].beat > cur.beat ||
                         (nv[j].beat == cur.beat && ((nv[j].kind == 1) ? 0 : 1) > curRank))) {
            nv[j + 1] = nv[j];
            --j;
        }
        nv[j + 1] = cur;
    }
    for(s32 i = 0; i < nn; ++i) emit(nv[i]);

    // Метроном: каждая целая доля на пройденном отрезке. Первая доля
    // такта — акцент (выше и чуть громче): по ней слышно, где такт.
    if(m_metro) {
        // Доля стучит ОДИН раз: при пересечении её начала (prev < b <= now).
        // Исключение — самый первый такт: доля 0 совпадает с пуском и
        // без явного включения пошла бы потерянной.
        const s32 b0 = (prev <= 0.0f) ? 0 : (s32)std::floor(prev) + 1;
        const s32 b1 = (s32)std::floor(now);
        for(s32 b = b0; b <= b1; ++b)
            emit({ (b % 4 == 0) ? 3 : 2, -1, 0 });
    }

    // Конец окна: самоостановка, плейхед на начало (пуск всегда с нуля).
    if(m_pos >= (f32)kBeats) {
        m_playing = false;
        m_pos = 0.0f;
    }
    return n;
}

}  // namespace Smp
