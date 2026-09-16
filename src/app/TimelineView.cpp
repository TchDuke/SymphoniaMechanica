#include "TimelineView.hpp"
#include "HID_draw.hpp"
#include "UiColors.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace Smp {

// Геометрия окна: рамка как у клавиатуры, слева — жутер с номерами дорожек.
TimelineView::TlGeo TimelineView::geo() const {
    TlGeo g;
    g.gx = m_rect.x + 8 + 26;
    g.gy = m_rect.y + 8;
    g.gw = m_rect.w - 16 - 26;
    g.gh = m_rect.h - 16;
    g.rowH = (g.gh > 0) ? g.gh / kChannelRows : 0;
    g.cellW = (g.gw > 0 && g.rowH > 0) ? g.gw / Timeline::kBeats : 0;
    return g;
}

TimelineView::TimelineView() {
    SetStyle({ 20, 20, 26, true, 48, 48, 58 });
}

void TimelineView::SetPlaceHandler(PlaceHandler fn, void* user) {
    m_place = fn;
    m_user = user;
}

void TimelineView::SetCursor(s32 row, s32 beat) {
    m_cursorRow = row;
    m_cursorBeat = beat;
}

s32 TimelineView::rowAt(s32 x, s32 y) const {
    TlGeo g = geo();
    if(g.rowH <= 0) return -1;
    if(x < g.gx || y < g.gy || x >= g.gx + g.gw || y >= g.gy + g.gh) return -1;
    s32 r = (y - g.gy) / g.rowH;
    if(r < 0) r = 0;
    if(r >= kChannelRows) r = kChannelRows - 1;
    return r;
}

s32 TimelineView::beatAt(s32 x, s32 y) const {
    TlGeo g = geo();
    if(g.cellW <= 0) return -1;
    if(x < g.gx || y < g.gy || x >= g.gx + g.gw || y >= g.gy + g.gh) return -1;
    s32 beat = (s32)std::floor(((f32)(x - g.gx) / (f32)g.cellW) + 0.5f);
    if(beat < 0) beat = 0;
    if(beat >= Timeline::kBeats) beat = Timeline::kBeats - 1;
    return beat;
}

s32 TimelineView::beatToX(f32 beat) const {
    TlGeo g = geo();
    if(g.cellW <= 0) return g.gx;
    return g.gx + (s32)std::lround(beat * (f32)g.cellW);
}

void TimelineView::Draw(HID_context& ctx) {
    if(!IsVisible()) return;
    const s32 px = m_rect.x + 8, py = m_rect.y + 8;
    const s32 pw = m_rect.w - 16, ph = m_rect.h - 16;
    if(pw <= 0 || ph <= 0) {
        HID_box::Draw(ctx);
        return;
    }
    HID_box::Draw(ctx);   // фон и рамка — базовым стилем

    TlGeo g = geo();
    if(g.rowH <= 0 || g.cellW <= 0) return;
    const s32 charH = ctx.font ? ctx.font->CharHeight(ctx.fontScale) : 16;

    // Полосы: чередованием видно дорожки без жирных линий.
    for(s32 r = 0; r < kChannelRows; ++r) {
        const s32 ry = py + r * g.rowH;
        const s32 rr = (r & 1) ? 24 : 28;
        const s32 rb = (r & 1) ? 31 : 36;
        HID_fill(ctx, g.gx, ry, g.gw, g.rowH, rr, rr, rb);
        HID_frame(ctx, g.gx, ry, g.gw, g.rowH, 40, 40, 50);
        // Номер дорожки — в жутере, мелкий и приглушённый: это служебная шкала.
        char num[8];
        std::snprintf(num, sizeof num, "%d", r + 1);
        HID_drawText(ctx, px + 4, ry + (g.rowH - charH) / 2, num, 92, 92, 108);
    }

    // Сетка метронома: тонкая — доля, жирная — такт (каждая 4-я). К вставке
    // прилипается именно сюда, оттого и рисуется до нот.
    for(s32 b = 0; b <= Timeline::kBeats; ++b) {
        const bool bar = (b % 4 == 0);
        const s32 x = g.gx + b * g.cellW;
        HID_fill(ctx, x, py, bar ? 2 : 1, g.gh, bar ? 74 : 44, bar ? 74 : 44, bar ? 88 : 54);
    }

    // Ноты: цвет — инструментом (волной), как в меню «Сигнал». Строки
    // выше канала 16 не видны в этом виде (они — сетка «клавиш»).
    s32 n = 0;
    if(m_tl) n = m_tl->CopyNotes(m_sn, Timeline::kMaxNote);
    for(s32 i = 0; i < n; ++i) {
        const Timeline::Note& e = m_sn[i];
        if(e.row < 0 || e.row >= kChannelRows) continue;
        const s32 x = g.gx + e.beat * g.cellW;
        const s32 y = py + e.row * g.rowH;
        const s32 wq = (e.dur > 0) ? e.dur : 1;
        s32 wd = wq * g.cellW - 2;
        if(wd < 3) wd = 3;
        const s32* c = WaveColor(e.wave);
        HID_fill(ctx, x + 1, y + 2, wd, g.rowH - 4, c[0], c[1], c[2]);
        HID_frame(ctx, x + 1, y + 2, wd, g.rowH - 4, c[0] / 2, c[1] / 2, c[2] / 2);
    }

    // Прицел под курсором: приглушённо, чтобы не притягивать глаз намертво.
    if(m_hoverRow >= 0 && m_hoverBeat >= 0 && !(m_cursorRow == m_hoverRow && m_cursorBeat == m_hoverBeat)) {
        const s32 x = g.gx + m_hoverBeat * g.cellW;
        const s32 y = py + m_hoverRow * g.rowH;
        HID_frame(ctx, x, y, g.cellW, g.rowH, 66, 66, 80);
    }

    // Маркер точки вставки: янтарная рамка — куда ляжет следующая нота.
    if(m_cursorRow >= 0 && m_cursorBeat >= 0) {
        const s32 x = g.gx + m_cursorBeat * g.cellW;
        const s32 y = py + m_cursorRow * g.rowH;
        HID_frame(ctx, x, y, g.cellW, g.rowH, 240, 200, 80);
        HID_frame(ctx, x + 2, y + 2, g.cellW - 4, g.rowH - 4, 240, 200, 80);
    }

    // Плейхед — только во время проигрывания (иначе он бы лежал на нуле
    // и мешал цели).
    if(m_tl && m_tl->Playing()) {
        const s32 x = beatToX(m_tl->PosBeats());
        HID_fill(ctx, x, py, 2, g.gh, 235, 90, 80);
    }
}

void TimelineView::ProcessEvent(const HID_event& event, bool& handled) {
    if(!IsActive() || !IsVisible()) return;

    if(event.type == HID_event_type::PointerMove) {
        m_hoverRow = rowAt(event.x, event.y);
        m_hoverBeat = beatAt(event.x, event.y);
        return;
    }
    if(event.type != HID_event_type::PointerDown) return;
    if(event.rightButton || event.middleButton) return;   // право — резерв (удаление — позже)

    const s32 row = rowAt(event.x, event.y);
    const s32 beat = beatAt(event.x, event.y);
    if(row < 0) return;
    if(m_place) m_place(row, beat, m_user);
    handled = true;
}

}  // namespace Smp
