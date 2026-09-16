#include "RollView.hpp"
#include "HID_draw.hpp"
#include "PianoView.hpp"
#include "UiColors.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace Smp {

// Строка 0 (До2, НЧ) — ВНИЗУ; строка 35 (Си4, ВЧ) — вверху.
RollView::RlGeo RollView::geo() const {
    RlGeo g;
    g.gy = m_rect.y + 8;
    g.gh = m_rect.h - 16;
    g.gx = m_rect.x + 8 + kStripW + 2;
    g.gw = m_rect.w - 16 - kStripW - 2;
    g.rowH = (g.gh > 0) ? g.gh / kKeys : 0;
    g.cellW = (g.gw > 0) ? g.gw / Timeline::kBeats : 0;
    return g;
}

RollView::RollView() {
    SetStyle({ 20, 20, 26, true, 48, 48, 58 });
    std::memset(m_active, 0, sizeof m_active);
}

void RollView::SetNoteHandler(NoteHandler fn, void* user) {
    m_note = fn;
    m_noteUser = user;
}

void RollView::SetCellHandler(CellHandler fn, void* user) {
    m_cell = fn;
    m_cellUser = user;
}

void RollView::SetPaintHandler(PaintHandler fn, void* user) {
    m_paint = fn;
    m_paintUser = user;
}

void RollView::SetPaintEndHandler(PaintEndHandler fn, void* user) {
    m_paintEnd = fn;
    m_paintEndUser = user;
}

void RollView::SetEraseHandler(EraseHandler fn, void* user) {
    m_erase = fn;
    m_eraseUser = user;
}

void RollView::SetActiveNotes(const s32* notes, s32 count) {
    if(count > 16) count = 16;
    m_activeN = count;
    for(s32 i = 0; i < count; ++i) m_active[i] = notes[i];
}

static bool isBlackKey(s32 midi) {
    const s32 pc = midi % 12;
    return (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
}

// Белые тоника внутри октавы (pc): i-я белая — pc, и наоборот.
static const s32 kWhitePc[7] = { 0, 2, 4, 5, 7, 9, 11 };

s32 RollView::whiteIndex(s32 row) {
    if(row < 0 || row >= kKeys) return -1;
    // pc клавиши строки = pc ноты (kFirstMidi = До2 — тоника), октава = row/12.
    const s32 pc = Timeline::RowMidi(row) % 12;
    for(s32 i = 0; i < 7; ++i)
        if(kWhitePc[i] == pc)
            return (row / 12) * 7 + i;
    return -1;
}

RollView::KGeo RollView::kgeo() const {
    KGeo k;
    k.x = m_rect.x + 8;
    k.y = m_rect.y + 8;
    k.w = kStripW;
    k.h = m_rect.h - 16;
    k.ch = (k.h > 0) ? k.h / kWhiteKeys : 0;   // клетка БЕЛОЙ клавиши
    return k;
}

s32 RollView::yForRow(s32 row) const {
    RlGeo g = geo();
    return g.gy + (kKeys - 1 - row) * g.rowH;
}

s32 RollView::rowAtY(s32 y) const {
    RlGeo g = geo();
    if(g.rowH <= 0) return -1;
    if(y < g.gy || y >= g.gy + g.rowH * kKeys) return -1;
    s32 idx = (y - g.gy) / g.rowH;          // 0 — верхняя строка
    return kKeys - 1 - idx;                 // -> нумерация снизу вверх
}

s32 RollView::beatAtX(s32 x) const {
    RlGeo g = geo();
    if(g.cellW <= 0) return -1;
    if(x < g.gx || x >= g.gx + g.cellW * Timeline::kBeats) return -1;
    s32 beat = (s32)std::floor(((f32)(x - g.gx) / (f32)g.cellW) + 0.5f);
    if(beat < 0) beat = 0;
    if(beat >= Timeline::kBeats) beat = Timeline::kBeats - 1;
    return beat;
}

// Доля под X без проверки попадания (для растяжки: мышь может уйти за край —
// прилипаем к ближайшему краю сетки).
s32 RollView::beatClamped(s32 x) const {
    RlGeo g = geo();
    if(g.cellW <= 0) return 0;
    s32 beat = (s32)std::floor(((f32)(x - g.gx) / (f32)g.cellW) + 0.5f);
    if(beat < 0) beat = 0;
    if(beat >= Timeline::kBeats) beat = Timeline::kBeats - 1;
    return beat;
}

s32 RollView::keyAt(s32 x, s32 y) const {
    const KGeo k = kgeo();
    if(k.ch <= 0) return -1;
    if(x < k.x || x >= k.x + k.w || y < k.y || y >= k.y + k.h) return -1;
    // Чёрные ПЕРВЫМИ: они лежат поверх белых, и под вкладкой физически
    // белая клавиша, а играть надо чёрную.
    for(s32 r = 0; r < kKeys; ++r) {
        const s32 midi = Timeline::RowMidi(r);
        if(!isBlackKey(midi)) continue;
        s32 tx, ty, tw, th;
        if(!blackTab(r, tx, ty, tw, th)) continue;
        if(x >= tx && x < tx + tw && y >= ty && y < ty + th) return midi;
    }
    // Белые: клетка по Y, 0 = самая нижняя (До2).
    const s32 w = (k.y + k.h - 1 - y) / k.ch;
    if(w < 0 || w >= kWhiteKeys) return -1;
    return Timeline::RowMidi((w / 7) * 12 + kWhitePc[w % 7]);
}

bool RollView::blackTab(s32 r, s32& tx, s32& ty, s32& tw, s32& th) const {
    const KGeo k = kgeo();
    if(k.ch < 8) return false;      // на крошечной клетке вкладка — невидимая точка
    // ГРАНЬ между белой снизу (w(r−1)) и белой сверху (w(r+1)): верх
    // клетки w(r−1).
    const s32 wBelow = whiteIndex(r - 1);
    const s32 ySeam = k.y + (kWhiteKeys - 1 - wBelow) * k.ch;
    th = k.ch * 58 / 100;
    if(th < 6) th = 6;
    tw = kBlackW;
    // СЕРЕДИНА ГРАНИ — вкладка делит две соседние белые пополам и занимает
    // их середину (без сдвига к соседке: слово владельца 16.09.2026). По
    // глубине короче белых и стоит у дальнего торца — рояль на 90°:
    // белая видна и выше вкладки, и ниже.
    ty = ySeam - th / 2;
    tx = k.x + k.w - kBlackW;
    return true;
}

void RollView::Draw(HID_context& ctx) {
    if(!IsVisible()) return;
    const s32 py = m_rect.y + 8;
    const s32 pw = m_rect.w - 16, ph = m_rect.h - 16;
    if(pw <= 0 || ph <= 0) {
        HID_box::Draw(ctx);
        return;
    }
    HID_box::Draw(ctx);   // фон и рамка — базовым стилем

    RlGeo g = geo();
    if(g.rowH <= 0 || g.cellW <= 0) return;
    const s32 charH = ctx.font ? ctx.font->CharHeight(ctx.fontScale) : 16;

    // ── Вертикальная клавиатура — рояль на 90° (слово владельца 16.09.2026:
    // «как в нормальном пианинном режиме, только короче и вертикально»):
    // 21 белая — НЕПРЕРЫВНЫЙ столб целых клеток (НЧ внизу), 15 чёрных —
    // вкладки НА ГРАНЯХ двух соседних белил: по середине грани (делят две
    // белые пополам), короче белых по глубине, у дальнего торца. Чёрные
    // поверх: белая видна и выше, и ниже вкладки — читается как клавиши,
    // а не как полосатый список. ────────────────────────────────────────────
    const KGeo k = kgeo();
    for(s32 w = 0; w < kWhiteKeys; ++w) {
        const s32 midi = Timeline::RowMidi((w / 7) * 12 + kWhitePc[w % 7]);
        const s32 y = k.y + (kWhiteKeys - 1 - w) * k.ch;
        bool act = false;
        for(s32 i = 0; i < m_activeN; ++i)
            if(m_active[i] == midi) act = true;
        const bool hov = !m_keyHeld && m_hoverKey == midi;

        HID_fill(ctx, k.x, y, k.w, k.ch,
                 act ? 226 : 222, act ? 188 : 220, act ? 110 : 226);
        HID_frame(ctx, k.x, y, k.w, k.ch, 34, 34, 42);
        if(hov) HID_frame(ctx, k.x + 1, y + 1, k.w - 2, k.ch - 2, 240, 240, 250);

        // Подпись: имя ноты БЕЗ октавы (чёрные не подписываются — их рисунок
        // и так читается); на До — с октавой: трёх «До» в палитре, по них
        // считаны остальные. Клетка шрифта 10×18: «Соль» — 40 px, влезает.
        if(k.ch >= charH + 2) {
            char name[16];
            PianoView::NoteName(midi, name, (s32)sizeof name);
            s32 len = (s32)std::strlen(name);
            if(len > 0 && name[len - 1] >= '0' && name[len - 1] <= '9' && midi % 12 != 0)
                name[len - 1] = 0;   // октава — только на До
            const s32 tw = HID_textWidth(ctx, name);
            if(tw <= k.w - 4)
                HID_drawText(ctx, k.x + (k.w - tw) / 2, y + (k.ch - charH) / 2, name,
                             90, 92, 104);
        }
    }
    // Чёрные — короткие вкладки поверх: середина грани двух белил, правый
    // край у дальнего торца — как на настоящем инструменте.
    for(s32 r = 0; r < kKeys; ++r) {
        const s32 midi = Timeline::RowMidi(r);
        if(!isBlackKey(midi)) continue;
        s32 tx, ty, tw, th;
        if(!blackTab(r, tx, ty, tw, th)) continue;
        bool act = false;
        for(s32 i = 0; i < m_activeN; ++i)
            if(m_active[i] == midi) act = true;
        const bool hov = !m_keyHeld && m_hoverKey == midi;
        HID_fill(ctx, tx, ty, tw, th,
                 act ? 120 : 30, act ? 160 : 30, act ? 220 : 36);
        HID_frame(ctx, tx, ty, tw, th, 16, 16, 20);
        if(hov) HID_frame(ctx, tx + 1, ty + 1, tw - 2, th - 2, 240, 240, 250);
    }

    // ── Сетка: фон ровный, строки-октавы (До) чуть заметнее. ───────────────
    HID_fill(ctx, g.gx, py, g.gw, g.gh, 24, 24, 31);
    for(s32 r = 0; r < kKeys; ++r) {
        if(r % 12 != 0) continue;   // только До
        const s32 y = yForRow(r);
        if(y + g.rowH > py + g.gh) continue;
        HID_fill(ctx, g.gx, y, g.gw, g.rowH, 27, 27, 35);
    }

    // Сетка метронома: тонкая — доля, жирная — такт (каждая 4-я).
    for(s32 b = 0; b <= Timeline::kBeats; ++b) {
        const bool bar = (b % 4 == 0);
        const s32 x = g.gx + b * g.cellW;
        HID_fill(ctx, x, py, bar ? 2 : 1, g.gh, bar ? 74 : 44, bar ? 74 : 44, bar ? 88 : 54);
    }

    // Ноты той же общей ленты: цвет — инструментом.
    s32 n = 0;
    if(m_tl) n = m_tl->CopyNotes(m_sn, Timeline::kMaxNote);
    for(s32 i = 0; i < n; ++i) {
        const Timeline::Note& e = m_sn[i];
        const s32 x = g.gx + e.beat * g.cellW;
        const s32 y = yForRow(e.row);
        const s32 wq = (e.dur > 0) ? e.dur : 1;
        s32 wd = wq * g.cellW - 2;
        if(wd < 3) wd = 3;
        const s32* c = WaveColor(e.wave);
        HID_fill(ctx, x + 1, y + 2, wd, g.rowH - 4, c[0], c[1], c[2]);
        HID_frame(ctx, x + 1, y + 2, wd, g.rowH - 4, c[0] / 2, c[1] / 2, c[2] / 2);
    }

    // Прицел под курсором (своя клетка — в своей строке).
    if(m_hoverRow >= 0 && m_hoverBeat >= 0 && !m_drag) {
        const s32 x = g.gx + m_hoverBeat * g.cellW;
        const s32 y = yForRow(m_hoverRow);
        HID_frame(ctx, x, y, g.cellW, g.rowH, 66, 66, 80);
    }

    // Плейхед — только во время проигрывания (иначе он лёг на ноль и мешал).
    if(m_tl && m_tl->Playing()) {
        const s32 x = g.gx + (s32)std::lround(m_tl->PosBeats() * (f32)g.cellW);
        HID_fill(ctx, x, py, 2, g.gh, 235, 90, 80);
    }
}

void RollView::ProcessEvent(const HID_event& event, bool& handled) {
    if(!IsActive() || !IsVisible()) return;

    if(event.type == HID_event_type::PointerMove) {
        const s32 row = rowAtY(event.y);
        m_hoverRow = row;
        m_hoverBeat = (row >= 0) ? beatAtX(event.x) : -1;
        m_hoverKey = keyAt(event.x, event.y);
        if(m_drag) {
            // Растяжка идёт только ВДОЛЬ ОСИ ВРЕМЕНИ: строка — та, где
            // начался клик; доля — под курсором, с прилипом к краям.
            const s32 b = beatClamped(event.x);
            if(b != m_dragCur) {
                m_dragCur = b;
                m_dragPainted = true;
                if(m_paint) {
                    s32 lo = m_dragStart, hi = m_dragCur;
                    if(hi < lo) { s32 t = lo; lo = hi; hi = t; }
                    m_paint(m_dragRow, lo, hi, m_paintUser);
                }
            }
        }
        return;
    }

    if(event.type == HID_event_type::PointerDown) {
        if(event.middleButton) return;   // средняя — не наша
        if(event.rightButton) {
            const s32 row = rowAtY(event.y);
            if(row >= 0) {
                const s32 beat = beatAtX(event.x);
                if(beat >= 0 && m_erase) m_erase(row, beat, m_eraseUser);
            }
            handled = true;
            return;
        }
        // Левая: либо клавиша (звучит), либо клетка (переключается и
        // готовит растяжку).
        const s32 key = keyAt(event.x, event.y);
        if(key >= 0 && m_note) {
            m_keyHeld = true;
            m_keyMidi = key;
            m_note(1, key, m_noteUser);
            handled = true;
            return;
        }
        const s32 row = rowAtY(event.y);
        if(row >= 0) {
            const s32 beat = beatAtX(event.x);
            if(beat >= 0 && m_cell) {
                // Ctrl в миг клика — клетка-стаккато (решает экран).
                m_cell(row, beat, event.ctrl, m_cellUser);
                m_drag = true;
                m_dragRow = row;
                m_dragStart = m_dragCur = beat;
            }
        }
        handled = true;
        return;
    }

    if(event.type != HID_event_type::PointerUp) return;
    if(m_keyHeld) {
        m_keyHeld = false;
        if(m_keyMidi >= 0 && m_note) m_note(0, m_keyMidi, m_noteUser);
        m_keyMidi = -1;
    }
    if(m_drag && m_dragPainted && m_paintEnd)
        m_paintEnd(m_dragRow, m_paintEndUser);   // живой тон штриха — отпускаем
    m_drag = false;
    m_dragPainted = false;
    m_hoverKey = keyAt(event.x, event.y);
    handled = true;
}

}  // namespace Smp
