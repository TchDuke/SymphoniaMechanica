#pragma once
#include "Core/Types.hpp"

#include "Timeline.hpp"
#include "HID_box.hpp"

namespace Smp {

// Вид «Клавиши» — лента для людей без навыков игры: слева вертикальная
// клавиатура короткими клавишами (НЧ внизу, ВЧ вверху), по одну строку на
// клавишу; справа сетка на 64 доли. Основная работа — мышкой:
//   клик ЛКМ по клетке    — клетка переключается (нота встала / снята);
//   Ctrl + клик ЛКМ       — клетка-стаккато: в доле звучит короткая нота,
//     по клетке             остаток доли — пауза (ряды слитных клеток
//                           прерывает в обе стороны);
//   клик ЛКМ + движение   — рисуется штрих: все клетки между начальной долей
//     вдоль оси времени      и текущей становятся ОДНОЙ сплошной нотой
//                           (прямоугольником), живо, до отпускания;
//   клик ПКМ по клетке    — нота снимается;
//   зажатая ЛКМ на клавише — клавиша звучит (прослушать тон до вставки).
//
// Одна лента с видом «16 каналов» (Timeline, 36 дорожек = 36 клавиш C2..B4):
// виджет не владеет лентой, отрисовывает её снимком и докладывает клики
// колбэками; что и как ставить в ленту — решает экран.

class RollView : public HID_box {
public:
    RollView();

    enum { kStripW = 44 };   // ширина вертикальной клавиатуры (влезает «Соль»)
    enum { kKeys = Timeline::kRows };   // 36 клавиш = 36 строк
    enum { kWhiteKeys = 21 };  // белых в C2..B4: столб непрерывных белил палитры
    enum { kBlackW = 28 };   // длина чёрной вкладки по глубине (короче белой)

    // Вертикальная клавиатура: on=true — клавиша (нота) встала.
    using NoteHandler = void (*)(s32 on, s32 midi, void* user);
    void SetNoteHandler(NoteHandler fn, void* user);
    // Клик ЛКМ по клетке (переключение решает экран): (строка 0..35, доля),
    // ctrl — зажат ли Ctrl в миг клика (клетка-стаккато).
    using CellHandler = void (*)(s32 row, s32 beat, bool ctrl, void* user);
    void SetCellHandler(CellHandler fn, void* user);
    // Штрих: все клетки строки от доли lo до hi включительно (lo <= hi).
    // Зовётся живо, при каждом шаге доли по ходу движения мыши (диапазон
    // может и сокращаться) — экран из клеток выстраивает штрих.
    using PaintHandler = void (*)(s32 row, s32 lo, s32 hi, void* user);
    void SetPaintHandler(PaintHandler fn, void* user);
    // Окончание растяжки: мышь отпущена, И штрих реально рисовался
    // (доля хоть раз сменилась) — экран отпускает живой звук штриха.
    using PaintEndHandler = void (*)(s32 row, void* user);
    void SetPaintEndHandler(PaintEndHandler fn, void* user);
    // Клик ПКМ по занятой клетке.
    using EraseHandler = void (*)(s32 row, s32 beat, void* user);
    void SetEraseHandler(EraseHandler fn, void* user);

    // Лента для снимка. Снимается при каждом Draw.
    void SetTimeline(const Timeline* tl) { m_tl = tl; }
    // Подсветка клавиш: миди-номера активных голосов (как у PianoView).
    void SetActiveNotes(const s32* notes, s32 count);

    void Draw(HID_context& ctx) override;
    void ProcessEvent(const HID_event& event, bool& handled) override;

private:
    struct RlGeo {
        s32 gx, gy, gw, gh;   // область сетки (правее клавиш)
        s32 rowH, cellW;
    };
    RlGeo geo() const;
    struct KGeo {
        s32 x, y, w, h;       // полоса палитры (левее сетки)
        s32 ch;               // высота клетки БЕЛОЙ клавиши
    };
    KGeo kgeo() const;        // геометрия вертикальной клавиатуры
    // Индекс белой клавиши (0 = До2 внизу) по строке; -1 — чёрная клавиша.
    static s32 whiteIndex(s32 row);
    s32 rowAtY(s32 y) const;      // строка по Y (строка 0 — ВНИЗУ)
    s32 beatAtX(s32 x) const;     // доля по X, с прилипанием; -1 мимо сетки
    s32 beatClamped(s32 x) const; // доля по X без проверки попадания (края)
    s32 keyAt(s32 x, s32 y) const;   // миди клавиши под точкой; -1 мимо
    s32 yForRow(s32 row) const;
    // Прямоугольник чёрной клавиши строки r: вкладка на ГРАНИ двух соседних
    // белил — занимает середину грани (делит две белые пополам), короче
    // белых по глубине и у дальнего торца: рояль, повёрнутый на 90°.
    // false — клетка слишком мала, вкладка невидима.
    bool blackTab(s32 r, s32& tx, s32& ty, s32& tw, s32& th) const;

    NoteHandler    m_note = nullptr;
    void*          m_noteUser = nullptr;
    CellHandler    m_cell = nullptr;
    void*          m_cellUser = nullptr;
    PaintHandler   m_paint = nullptr;
    void*          m_paintUser = nullptr;
    PaintEndHandler m_paintEnd = nullptr;
    void*          m_paintEndUser = nullptr;
    EraseHandler   m_erase = nullptr;
    void*          m_eraseUser = nullptr;

    const Timeline* m_tl = nullptr;
    Timeline::Note  m_sn[Timeline::kMaxNote];

    s32 m_active[16];
    s32 m_activeN = 0;

    s32 m_hoverRow = -1;     // клетка под курсором (сетка)
    s32 m_hoverBeat = -1;
    s32 m_hoverKey = -1;     // клавиша под курсором (лента клавиш)

    bool  m_drag = false;    // рисуется штрих
    bool  m_dragPainted = false;   // доля сменилась — штрих реально появился
    s32   m_dragRow = -1;
    s32   m_dragStart = 0;   // доля, где началась растяжка
    s32   m_dragCur = 0;     // текущий конец, по ходу движения мыши
    bool  m_keyHeld = false; // зажатая клавиша (прослушивание)
    s32   m_keyMidi = -1;
};

}  // namespace Smp
