#include "PianoView.hpp"
#include "HID_draw.hpp"

#include <cstdio>
#include <cstring>

namespace Smp {

const s32 PianoView::kWhite[7]     = { 0, 2, 4, 5, 7, 9, 11 };
const s32 PianoView::kBlackMidi[5] = { 1, 3, 6, 8, 10 };
const s32 PianoView::kBlackAfter[5] = { 0, 1, 3, 4, 5 };

PianoView::PianoView() {
    SetStyle({ 20, 20, 26, true, 48, 48, 58 });
    std::memset(m_active, 0, sizeof m_active);
}

void PianoView::SetNoteHandler(NoteHandler fn, void* user) {
    m_note = fn;
    m_user = user;
}

void PianoView::SetActiveNotes(const s32* notes, s32 count) {
    if(count > 16) count = 16;
    m_activeN = count;
    for(s32 i = 0; i < count; ++i) m_active[i] = notes[i];
}

bool PianoView::isActive(s32 midi) const {
    for(s32 i = 0; i < m_activeN; ++i)
        if(m_active[i] == midi) return true;
    return false;
}

void PianoView::NoteName(s32 midi, char* out, s32 size) {
    if(size <= 0) return;
    // Имя — по классу высоты целиком. Чётность класса чёрную клавишу НЕ
    // выдаёт: после Ми полутон Ми–Фа сбивает её, и «pc>>1 + диез по
    // нечётности» давало Фа → «Ми#», Соль → «Фа#», Си → «Ля#».
    static const char* names[12] = { "До", "До#", "Ре", "Ре#", "Ми", "Фа",
                                     "Фа#", "Соль", "Соль#", "Ля", "Ля#", "Си" };
    const s32 pc = midi % 12;
    const s32 octave = midi / 12 - 1;
    // Номер октавы — по MIDI целиком: C4=60 → 60/12−1=4, и для диёзов то же
    // самое число (До#4=61 → 4). Отдельных правил для чёрных нет.
    std::snprintf(out, (size_t)size, "%s%d", names[pc], octave);
}

s32 PianoView::BlackOff100(s32 pianoClass) {
    switch(pianoClass) {
        case 1:  return  -5;   // До#: ближе к До
        case 3:  return  +5;   // Ре#: ближе к Ми
        case 6:  return  -5;   // Фа#: ближе к Фа
        case 8:  return   0;   // Соль#: по центру
        case 10: return  +5;   // Ля#: ближе к Си
    }
    return 0;
}

void PianoView::blackRect(s32 oct, s32 b, s32& bx, s32& by, s32& bw, s32& bh) const {
    const s32 px = m_rect.x + 8, py = m_rect.y + 8;
    const s32 pw = m_rect.w - 16, ph = m_rect.h - 16;
    const s32 ww = pw / whiteCount();
    bw = (ww * 58 / 100 > 8) ? ww * 58 / 100 : 8;
    bh = ph * 62 / 100;
    by = py;
    // Центр на грани между белыми + сдвиг к соседней белой (не по центру).
    bx = px + (oct * 7 + kBlackAfter[b] + 1) * ww
         + BlackOff100(kBlackMidi[b]) * ww / 100 - bw / 2;
}

s32 PianoView::whiteMidi(s32 index) const {
    if(index < 0 || index >= whiteCount()) return -1;
    const s32 oct = index / 7;
    const s32 pos = index % 7;
    return kFirstMidi + 12 * oct + kWhite[pos];
}

s32 PianoView::NoteAtPoint(s32 x, s32 y) const {
    const s32 px = m_rect.x + 8, py = m_rect.y + 8;
    const s32 pw = m_rect.w - 16, ph = m_rect.h - 16;
    if(x < px || y < py || x >= px + pw || y >= py + ph) return -1;
    const s32 ww = pw / whiteCount();
    if(ww <= 0) return -1;
    // Чёрные верхние и короче — их спрашиваем ПЕРВЫМИ: сверху под чёрной
    // физически белая, а играть надо чёрную.
    for(s32 oct = 0; oct < kOctaves; ++oct) {
        for(s32 b = 0; b < 5; ++b) {
            s32 bx, by, bw, bh;
            blackRect(oct, b, bx, by, bw, bh);
            if(x >= bx && x < bx + bw && y >= py && y < py + bh)
                return kFirstMidi + 12 * oct + kBlackMidi[b];
        }
    }
    const s32 idx = (x - px) / ww;
    if(idx < 0 || idx >= whiteCount()) return -1;
    return whiteMidi(idx);
}

void PianoView::press(s32 midi) {
    if(midi >= 0 && m_note) m_note(1, midi, m_user);
}

void PianoView::release(s32 midi) {
    if(midi >= 0 && m_note) m_note(0, midi, m_user);
}

void PianoView::Draw(HID_context& ctx) {
    if(!IsVisible()) return;

    const s32 px = m_rect.x + 8, py = m_rect.y + 8;
    const s32 pw = m_rect.w - 16, ph = m_rect.h - 16;
    if(pw <= 0 || ph <= 0) {
        HID_box::Draw(ctx);
        return;
    }
    HID_box::Draw(ctx);   // фон и рамка — базовым стилем

    const s32 ww = pw / whiteCount();
    const s32 charH = ctx.font ? ctx.font->CharHeight(ctx.fontScale) : 16;

    for(s32 i = 0; i < whiteCount(); ++i) {
        const s32 midi = whiteMidi(i);
        const s32 x = px + i * ww;
        const bool act = isActive(midi);
        const bool hov = !m_held && m_hover == midi;
        // Белая клавиша: обычная — светлая, активная — янтарная (звучит).
        HID_fill(ctx, x, py, ww, ph,
                 act ? 226 : 222, act ? 188 : 220, act ? 110 : 226);
        HID_frame(ctx, x, py, ww, ph, 34, 34, 42);
        if(hov) HID_frame(ctx, x + 1, py + 1, ww - 2, ph - 2, 240, 240, 250);
        // Подпись — только на До: её и так видно по рисунку чёрных, и имя
        // каждой клавиши на 21 белевой разнесло бы шум.
        if(i % 7 == 0 && ph > charH + 14) {
            char name[16];
            NoteName(midi, name, (s32)sizeof name);
            HID_drawText(ctx, x + 3, py + ph - charH - 5, name, 90, 92, 104);
        }
    }
    // Чёрные — поверх белых.
    for(s32 oct = 0; oct < kOctaves; ++oct) {
        for(s32 b = 0; b < 5; ++b) {
            const s32 midi = kFirstMidi + 12 * oct + kBlackMidi[b];
            s32 bx, by, bw, bh;
            blackRect(oct, b, bx, by, bw, bh);
            const bool act = isActive(midi);
            const bool hov = !m_held && m_hover == midi;
            HID_fill(ctx, bx, by, bw, bh,
                     act ? 120 : 30, act ? 160 : 30, act ? 220 : 36);
            HID_frame(ctx, bx, by, bw, bh, 16, 16, 20);
            if(hov) HID_frame(ctx, bx + 1, py + 1, bw - 2, bh - 2, 240, 240, 250);
        }
    }
}

void PianoView::ProcessEvent(const HID_event& event, bool& handled) {
    if(!IsActive()) return;

    if(event.type == HID_event_type::PointerMove) {
        const s32 n = NoteAtPoint(event.x, event.y);
        if(m_held) {
            // Скольжение по клавишам: смена ноты по факту ухода с клавиши,
            // пока кнопка не отпущена.
            if(n != m_down) {
                if(m_down >= 0) release(m_down);
                m_down = n;
                if(n >= 0) press(n);
            }
        } else if(n != m_hover) {
            m_hover = n;
        }
        return;   // движение — не событие для остальных виджетов
    }
    if(event.type != HID_event_type::PointerDown && event.type != HID_event_type::PointerUp)
        return;

    if(!Contains(event.x, event.y)) return;   // мимо клавиатуры — не наше

    if(event.type == HID_event_type::PointerDown) {
        if(event.rightButton || event.middleButton) return;
        m_held = true;
        const s32 n = NoteAtPoint(event.x, event.y);
        if(n >= 0) { m_down = n; press(n); }
        handled = true;
        return;
    }
    // PointerUp
    m_held = false;
    if(m_down >= 0) { release(m_down); m_down = -1; }
    // Скольжение: при удержании кнопки переход на другую клавишу
    const s32 n = NoteAtPoint(event.x, event.y);
    m_hover = n;
    handled = true;
}

}  // namespace Smp
