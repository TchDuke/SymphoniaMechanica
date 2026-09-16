#pragma once
#include "Core/Types.hpp"

#include "HID_box.hpp"

namespace Smp {

// Экранная фортепианная клавиатура: 3 октавы C2..B4 (MIDI 36..71).
// Мышь: зажатая левая кнопка в пределах клавиш — нота звучит, скольжение
// между клавишами переключает её (как по настоящей клавиатуре).
//
// Виджет сам держит состояние нажатия; наружу отдаёт события нот
// колбэком и принимает список активных нот для подсветки.

class PianoView : public HID_box {
public:
    PianoView();

    // on=true — нота встала, on=false — легла; midi — MIDI-номер.
    using NoteHandler = void (*)(s32 on, s32 midi, void* user);
    void SetNoteHandler(NoteHandler fn, void* user);

    // Подсветка: миди-номера активных голосов (до kVoices), хвост -1.
    void SetActiveNotes(const s32* notes /* >= 16 */, s32 count);

    // Нота под точкой (-1 — мимо), для статуса и агента.
    s32 NoteAtPoint(s32 x, s32 y) const;
    // Имя ноты UTF-8 («Фа#3») — и для подписей, и для строки статуса.
    static void NoteName(s32 midi, char* out, s32 size);
    // Смещение ЦЕНТРА чёрной клавиши к соседней белой, в сотнях ширины белой
    // (0 — ровно на грани): у настоящего фортепиано центр НЕ на границе.
    // Положительное — к высокой стороне. Делят с этим вид вид-сетка «Клавиши».
    static s32 BlackOff100(s32 pianoClass);

    enum { kFirstMidi = 36, kOctaves = 3 };

    void Draw(HID_context& ctx) override;
    void ProcessEvent(const HID_event& event, bool& handled) override;

private:
    // Белые клавиши одной октавы: C D E F G A B.
    static const s32 kWhite[7];
    // Чёрные: смещения от C + номер белой клавиши, ПОСЛЕ КОТОРОЙ чёрная.
    static const s32 kBlackMidi[5];   // {1, 3, 6, 8, 10}
    static const s32 kBlackAfter[5];  // {0, 1, 3, 4, 5}

    // Прямоугольник чёрной клавиши (центр на грани между белыми + BlackOff100).
    void blackRect(s32 oct, s32 b, s32& bx, s32& by, s32& bw, s32& bh) const;
    s32 whiteMidi(s32 index) const;
    s32 whiteCount() const { return 7 * kOctaves; }

    NoteHandler m_note = nullptr;
    void*       m_user = nullptr;
    bool        m_held = false;
    s32         m_down = -1;       // нота под зажатой кнопкой
    s32         m_hover = -1;      // нота под курсором (кнопка не зажата)
    s32         m_active[16];
    s32         m_activeN = 0;

    bool isActive(s32 midi) const;
    void press(s32 midi);
    void release(s32 midi);
};

}  // namespace Smp
