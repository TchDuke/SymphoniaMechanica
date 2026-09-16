#pragma once
#include "Core/Types.hpp"

#include "HID_box.hpp"
#include "HID_menu_bar.hpp"
#include "HID_menu.hpp"
#include "HID_form_dialog.hpp"
#include "HID_curve_dialog.hpp"
#include "PianoView.hpp"
#include "RollView.hpp"
#include "Timeline.hpp"
#include "TimelineView.hpp"
#include "Synth/Instrument.hpp"

namespace Smp {

class AudioBus;

// Корневой экран приложения: строка меню, окно времени, фортепианная
// клавиатура, строка состояния. Двигателем и звуковым трактом владеет
// main — экран лишь управляет ими и показывает их состояние.
//
// Два ВИДА ленты, переключение в меню «Вид» (одна лента, две проекции):
//   «16 каналов»  — окно времени (16 дорожек сетки метронома) + клавиатура;
//   «Клавиши»     — сетка 36 строк (по строке на клавишу) с вертикальной
//                    клавиатурой; мышь ставит клетки, штрихи — мышью.
// Лента молчит, пока её не пускать — проигрывание только по явному пуску.
// Вставленная клетка коротко превьюится (тон слышен сразу, без пуска).
//
// В виде «16 каналов» работает и прежний явный режим: «Вставка нот» —
// клавиша не играет, а ставит ноту в ленту в точку маркера (клик по времени).
class SmpScreen : public HID_box {
public:
    SmpScreen(AudioBus* bus);
    ~SmpScreen() override;

    void Resize(s32 w, s32 h);
    void SetRecordDir(const char* dir);
    void ApplySound(s32 wave, s32 volumePct);   // из настроек (до окна)
    void ApplyTransport(s32 bpm, bool metro, bool insert);
    // Инструмент из настроек: форма сигнала + огибающая (точки). Всё, что
    // не влезает в допуск — по заводу. Вызывать после ApplySound.
    void ApplyInstrument(u32 wave, const f32* envT, const f32* envV, s32 envN);
    const Instrument& CurrentInstrument() const { return m_instr; }
    // Вид ленты (0 — «16 каналов», 1 — «Клавиши»). Идемпотентно:
    // видимость виджета выставляется целиком, повторный вызов ничего не ломает.
    void SetView(s32 roll);

    bool WantsQuit() const { return m_quit; }
    s32  Wave() const { return m_wave; }
    s32  VolumePct() const { return m_volumePct; }
    s32  Bpm() const { return (s32)m_tl.Bpm(); }
    bool Metro() const { return m_tl.Metro(); }
    bool InsertMode() const { return m_insertMode; }
    bool ViewRoll() const { return m_viewRoll; }
    // Запись: пуск/стоп по факту состояния (у меню и у живого канала одно дело).
    void ToggleRecord();

    // Живой канал (AppAgent): те же действия, что у меню, по именам.
    void AgentNoteOn(s32 midi);
    void AgentNoteOff(s32 midi);
    void AgentWave(s32 wave);
    void AgentVolume(s32 volumePct);
    void AgentRecord(bool start);
    void AgentPlay();
    void AgentStop();
    // Вставка в ленту напрямую (без клавиатуры): дорожка, доля, нота.
    void AgentInsert(s32 row, s32 beat, s32 midi);
    void AgentClear();
    void AgentMetro(bool on);
    void AgentBpm(s32 bpm);
    void AgentInsertMode(bool on);
    // Вид-сетка «Клавиши»: то же, что и мышь, но по именам.
    void AgentView(bool roll);
    void AgentCell(s32 row, s32 beat);   // переключить клетку
    void AgentCellCtrl(s32 row, s32 beat);   // Ctrl-клетка: стаккато
    void AgentPaint(s32 row, s32 lo, s32 hi);   // штрих: lo..hi включ.
    // Превью ноты на всю огибающую (окно 2,0 с) — приёмка формы звука.
    void AgentPreview(s32 midi);
    // Краткая сводка огибающей (строка в reply) — «что сейчас на инструменте».
    void AgentEnv(char* reply, s32 replySize);
    // Сводка звуковой линии (разрывы/голодовки кольца) — приёмка звука по числам.
    void AgentLineStats(char* reply, s32 replySize);
    // Список ленты — для приёмки («что там лежит»): строка в reply.
    void AgentNotes(char* reply, s32 replySize);

    // Строка состояния — РОВНО та, что человек видит внизу окна (белый вход).
    const char* StatusText();
    HID_menu_bar& MenuBar() { return m_bar; }   // живому каналу (AppAgent)
    const Timeline& Tape() const { return m_tl; }   // для отладки/приёмки

    void Draw(HID_context& ctx) override;
    void ProcessEvent(const HID_event& event, bool& handled) override;
    bool WantsKeyboardCapture() const override {
        return m_bar.AnyOpen() || m_instrDlg.IsVisible() || m_envDlg.IsVisible();
    }

private:
    void buildMenus();
    void layout();
    void onMenu(const char* commandId);
    void onNote(s32 on, s32 midi);   // колбэк PianoView (статический мост)
    void onPlace(s32 row, s32 beat); // колбэк TimelineView (статический мост)
    void insertAtMarker(s32 midi);   // нота в ленту в точку маркера (+ сдвиг)
    // Вид-сетка «Клавиши» (колбэки RollView — те же статические мосты).
    void onRollNote(s32 on, s32 midi);    // вертикальная клавиша: живое звучание
    void onRollCell(s32 row, s32 beat, bool ctrl);   // клик: клетка встала/снята
    bool paintRange(s32 row, s32 lo, s32 hi);   // штрих в ленту: одна нота lo..hi
    void onRollPaint(s32 row, s32 lo, s32 hi);   // штрих мышью: + живой сплошной тон
    void onRollPaintEnd(s32 row);         // мышь отпущена: живой тон снят
    void onRollErase(s32 row, s32 beat);  // ПКМ: клетка снята
    void previewNote(s32 midi);           // короткий тон вставленной ноты
    // Меню «Инструмент»: форма — выбор + форма сигнала, кривая — редактор точек.
    void openInstrDlg();
    void openEnvDlg();
    void applyInstrDlg();
    void applyEnvDlg();
    void pushInstrumentToBus();           // текущий инструмент → в звук
    static void waveOptions(char* buf, s32 size);

    static void noteHandler(s32 on, s32 midi, void* user);
    static void placeHandler(s32 row, s32 beat, void* user);
    static void rollNoteHandler(s32 on, s32 midi, void* user);
    static void rollCellHandler(s32 row, s32 beat, bool ctrl, void* user);
    static void rollPaintHandler(s32 row, s32 lo, s32 hi, void* user);
    static void rollPaintEndHandler(s32 row, void* user);
    static void rollEraseHandler(s32 row, s32 beat, void* user);

    AudioBus*      m_bus;
    s32            m_wave = 0;
    s32            m_volumePct = 55;
    Instrument     m_instr;               // текущий инструмент (звук ноты)
    s32            m_lastNote = -1;   // последняя вставшая нота — для статуса
    s32            m_rollSustain = -1;   // живой штрих: зовущаяся высота (-1 — тишина)
    s32            m_menuH = 30;
    s32            m_statusH = 28;
    bool           m_quit = false;
    bool           m_insertMode = false;  // явный режим: клавиша вставляет
    bool           m_viewRoll = false;    // 0 — «16 каналов», 1 — «Клавиши»
    s32            m_markerRow = -1;      // точка вставки: дорожка, доля
    s32            m_markerBeat = -1;
    char           m_recordDir[384] = {0};

    Timeline       m_tl;        // лента: события + транспорт + метроном
    HID_menu_bar   m_bar;
    HID_menu       m_mWave;
    HID_menu       m_mVol;
    HID_menu       m_mTr;
    HID_menu       m_mMetro;
    HID_menu       m_mRec;
    HID_menu       m_mView;
    HID_menu       m_mInstr;
    HID_menu       m_mFile;
    TimelineView   m_tlView;
    RollView       m_roll;
    PianoView      m_piano;
    HID_form_dialog  m_instrDlg;          // «Настройки…»: инструмент + форма сигнала
    HID_curve_dialog m_envDlg{32};        // «Огибающая…»: редактор 32 точек
    char           m_status[768] = {0};
};

}  // namespace Smp
