#pragma once
#include "Core/Types.hpp"

#include "Timeline.hpp"
#include "HID_box.hpp"

namespace Smp {

// Вид «16 каналов»: 16 полос-дорожек (первые 16 строк общей ленты на 36),
// ось X в долях (64 доли = 16 тактов 4/4). Сетка-вид «клавиши» — вторая
// проекция той же ленты, где строка = клавиша.
//
// Мышью: щелчок левой кнопкой — точка вставки (дорожка + доля, с прилипанием
// к сетке); куда именно пойдёт нажатая клавиша, решает экран, виджет только
// сообщает координаты клика. Отрисовка — снимок ленты (события + плейхед):
// виджет не владеет лентой и не тикает, лента живёт своей жизнью.
//
// Сетка — та, что даёт метроном: доля — тонкая линия, такт — жирная. По жирным
// линиям и целятся с вставками: сетка и темп — одна система координат.

class TimelineView : public HID_box {
public:
    TimelineView();

    enum { kChannelRows = 16 };   // каналов в этом виде

    // Щелчок левой кнопкой в пределах окна: (дорожка 0..15, доля 0..63).
    using PlaceHandler = void (*)(s32 row, s32 beat, void* user);
    void SetPlaceHandler(PlaceHandler fn, void* user);

    // Лента для снимка (nullptr — пустая). Снимается при каждом Draw.
    void SetTimeline(const Timeline* tl) { m_tl = tl; }

    // Маркер точки вставки; (-1, -1) — нет (вставка не включена).
    void SetCursor(s32 row, s32 beat);

    void Draw(HID_context& ctx) override;
    void ProcessEvent(const HID_event& event, bool& handled) override;

private:
    struct TlGeo {
        s32 gx, gy, gw, gh;   // область нот (правее жутера)
        s32 rowH;             // высота полосы
        s32 cellW;            // ширина доли
    };
    TlGeo geo() const;
    s32 rowAt(s32 x, s32 y) const;      // -1 мимо
    s32 beatAt(s32 x, s32 y) const;     // -1 мимо; с прилипанием к сетке
    s32 beatToX(f32 beat) const;

    PlaceHandler          m_place = nullptr;
    void*                 m_user = nullptr;
    const Timeline*       m_tl = nullptr;
    Timeline::Note        m_sn[Timeline::kMaxNote];  // снимок для отрисовки
    s32                   m_cursorRow = -1;
    s32                   m_cursorBeat = -1;
    s32                   m_hoverRow = -1;
    s32                   m_hoverBeat = -1;
};

}  // namespace Smp
