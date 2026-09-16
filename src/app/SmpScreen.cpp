#include "SmpScreen.hpp"
#include "AudioBus.hpp"
#include "SynthEngine.hpp"
#include "HID_draw.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace Smp {

SmpScreen::SmpScreen(AudioBus* bus)
    : m_bus(bus),
      m_instr(InstrumentLib::At(0)),
      m_bar(8),
      m_mWave(4),
      m_mVol(12),
      m_mTr(4),
      m_mMetro(15),
      m_mRec(4),
      m_mView(2),
      m_mInstr(2),
      m_mFile(4) {
    m_style = { 26, 26, 32, false, 0, 0, 0 };
    m_bar.AddMenu(&m_mWave);
    m_bar.AddMenu(&m_mVol);
    m_bar.AddMenu(&m_mTr);
    m_bar.AddMenu(&m_mMetro);
    m_bar.AddMenu(&m_mRec);
    m_bar.AddMenu(&m_mView);
    m_bar.AddMenu(&m_mInstr);
    m_bar.AddMenu(&m_mFile);
    buildMenus();
    m_piano.SetNoteHandler(noteHandler, this);
    m_piano.SetActive(true);
    m_piano.SetVisible(true);
    m_tlView.SetPlaceHandler(placeHandler, this);
    m_tlView.SetTimeline(&m_tl);
    m_tlView.SetActive(true);
    m_tlView.SetVisible(true);
    m_roll.SetNoteHandler(rollNoteHandler, this);
    m_roll.SetCellHandler(rollCellHandler, this);
    m_roll.SetPaintHandler(rollPaintHandler, this);
    m_roll.SetPaintEndHandler(rollPaintEndHandler, this);
    m_roll.SetEraseHandler(rollEraseHandler, this);
    m_roll.SetTimeline(&m_tl);
    m_roll.SetActive(true);
    m_roll.SetVisible(false);    // вид выбирается менюм; старт — «16 каналов»
}

SmpScreen::~SmpScreen() = default;

void SmpScreen::buildMenus() {
    m_mWave.SetTitle("Сигнал");
    m_mWave.AddCheckCommandId("Пила",            "wave.0", false);
    m_mWave.AddCheckCommandId("Прямоугольник",   "wave.1", false);
    m_mWave.AddCheckCommandId("Треугольник",     "wave.2", false);
    m_mWave.AddCheckCommandId("Синус",           "wave.3", false);

    m_mVol.SetTitle("Громкость");
    for(s32 v = 10; v <= 100; v += 10) {
        char id[16], label[24];
        std::snprintf(id,    sizeof id,    "vol.%d", v);
        std::snprintf(label, sizeof label, "%d %%", v);
        m_mVol.AddCheckCommandId(label, id, false);
    }

    m_mTr.SetTitle("Транспорт");
    m_mTr.AddCommandId("Пуск", "play");
    m_mTr.AddCommandId("Стоп", "stop");
    m_mTr.AddCheckCommandId("Вставка нот", "ins.toggle", false);
    m_mTr.AddCommandId("Очистить ленту", "clear.tape");

    m_mMetro.SetTitle("Метроном");
    m_mMetro.AddCheckCommandId("Метроном", "metro.on", true);
    for(s32 b = 60; b <= 180; b += 10) {
        char id[16], label[24];
        std::snprintf(id,    sizeof id,    "bpm.%d", b);
        std::snprintf(label, sizeof label, "Темп: %d", b);
        m_mMetro.AddCheckCommandId(label, id, false);
    }

    m_mRec.SetTitle("Запись");
    m_mRec.AddCommandId("Записать / остановить", "rec.toggle");

    m_mView.SetTitle("Вид");
    m_mView.AddCheckCommandId("16 каналов", "view.tracks", false);
    m_mView.AddCheckCommandId("Клавиши", "view.roll", false);

    m_mInstr.SetTitle("Инструмент");
    m_mInstr.AddCommandId("Настройки...", "instr.cfg");
    m_mInstr.AddCommandId("Огибающая...", "env.edit");

    m_mFile.SetTitle("Файл");
    m_mFile.AddCommandId("Выход", "quit");
}

void SmpScreen::Resize(s32 w, s32 h) {
    SetRect(0, 0, w, h);
    layout();
}

void SmpScreen::SetRecordDir(const char* dir) {
    if(dir && dir[0]) std::snprintf(m_recordDir, sizeof m_recordDir, "%s", dir);
}

void SmpScreen::ApplySound(s32 wave, s32 volumePct) {
    if(wave >= 0 && wave < (s32)SynthEngine::WaveCount) m_wave = wave;
    if(volumePct >= 5 && volumePct <= 100) m_volumePct = volumePct;
    m_bus->SetWave((u32)m_wave);
    m_bus->SetVolume((f32)m_volumePct / 100.0f);
}

void SmpScreen::ApplyTransport(s32 bpm, bool metro, bool insert) {
    if(bpm >= 30 && bpm <= 240) m_tl.SetBpm((f32)bpm);
    m_tl.SetMetro(metro);
    m_insertMode = insert;
    m_bus->SetTimeline(&m_tl);
}

// ── ИНСТРУМЕНТ ────────────────────────────────────────────────────────────
// Инструмент — это звук ноты: форма сигнала + огибающая. Экран держит
// «выписанный» инструмент; в звук уходит LUT-огибающая и волна атомики.
void SmpScreen::ApplyInstrument(u32 wave, const f32* envT, const f32* envV, s32 envN) {
    m_instr = InstrumentLib::At(0);   // основа — заводской
    if(wave < (u32)SynthEngine::WaveCount) m_instr.wave = wave;
    if(envT && envV && envN >= 1 && envN <= 32) {
        for(s32 i = 0; i < envN; ++i) {
            f32 t = envT[i], v = envV[i];
            if(t < 0) t = 0; else if(t > (f32)SynthEngine::kEnvWindow) t = (f32)SynthEngine::kEnvWindow;
            if(v < 0) v = 0; else if(v > 1) v = 1;
            m_instr.envT[i] = t;
            m_instr.envV[i] = v;
        }
        m_instr.envN = envN;
    }
    pushInstrumentToBus();
}

void SmpScreen::pushInstrumentToBus() {
    m_wave = (s32)m_instr.wave;
    m_bus->SetWave((u32)m_wave);
    f32 lut[SynthEngine::kEnvLut];
    m_instr.BuildLut(lut);
    m_bus->SetEnvelope(lut);
}

void SmpScreen::waveOptions(char* buf, s32 size) {
    if(size <= 0) { if(buf) buf[0] = 0; return; }
    buf[0] = 0;
    s32 off = 0;
    for(s32 w = 0; w < (s32)SynthEngine::WaveCount; ++w)
        off += std::snprintf(buf + off, (size_t)(size - 1 - off), "%s%s",
                             w ? ";" : "", SynthEngine::WaveName((SynthEngine::WaveKind)w));
}

void SmpScreen::openInstrDlg() {
    char insOpts[256];
    insOpts[0] = 0;
    s32 off = 0;
    for(s32 i = 0; i < InstrumentLib::Count(); ++i)
        off += std::snprintf(insOpts + off, (size_t)(sizeof insOpts - 1 - off),
                             "%s%s", i ? ";" : "", InstrumentLib::At(i).name);
    char wOpts[160];
    waveOptions(wOpts, (s32)sizeof wOpts);
    m_instrDlg.Begin("Инструмент");
    m_instrDlg.AddChoice("Инструмент:", insOpts, m_instr.name);
    m_instrDlg.AddChoice("Форма сигнала:", wOpts,
                         SynthEngine::WaveName((SynthEngine::WaveKind)m_instr.wave));
    m_instrDlg.AddNote("Огибающая: уровень от 0 до 1 по окну 2,0 с — редактируется в «Огибающая...».");
    m_instrDlg.Open();
}

void SmpScreen::openEnvDlg() {
    m_envDlg.ClearPoints();
    const s32 n = (m_instr.envN < 32) ? m_instr.envN : 32;
    for(s32 i = 0; i < n; ++i)
        m_envDlg.AddPoint(m_instr.envT[i], (f64)m_instr.envV[i]);
    m_envDlg.Open("Огибающая", "время, с (0–2)", "уровень (0–1)", false);
}

void SmpScreen::applyInstrDlg() {
    const char* ins = m_instrDlg.FieldText(0);
    const char* wv = m_instrDlg.FieldText(1);
    s32 idx = 0;
    for(s32 i = 0; i < InstrumentLib::Count(); ++i)
        if(ins && !std::strcmp(InstrumentLib::At(i).name, ins)) idx = i;
    const Instrument& chosen = InstrumentLib::At(idx);
    // Новый инструмент — целиком заводской звук; если инструмент тот же,
    // уже отредактированная огибающая НЕ сбивается повторным ОК.
    if(std::strcmp(chosen.name, m_instr.name) != 0)
        m_instr = chosen;
    for(s32 w = 0; w < (s32)SynthEngine::WaveCount; ++w)
        if(wv && !std::strcmp(SynthEngine::WaveName((SynthEngine::WaveKind)w), wv))
            m_instr.wave = (u32)w;
    pushInstrumentToBus();
}

void SmpScreen::applyEnvDlg() {
    // Точки из редактора (отсортированы по X, до 32): в инструмент и в звук.
    const s32 n = m_envDlg.PointCount();
    if(n >= 1 && n <= 32) {
        for(s32 i = 0; i < n; ++i) {
            f32 t = (f32)m_envDlg.PointX(i);
            f32 v = (f32)m_envDlg.PointY(i);
            if(t < 0) t = 0; else if(t > (f32)SynthEngine::kEnvWindow) t = (f32)SynthEngine::kEnvWindow;
            if(v < 0) v = 0; else if(v > 1) v = 1;
            m_instr.envT[i] = t;
            m_instr.envV[i] = v;
        }
        m_instr.envN = n;
    }
    pushInstrumentToBus();
}

void SmpScreen::layout() {
    // Шрифт фиксированный: 18 — базовая высота строки при масштабе 1.0.
    m_menuH = 30;
    m_statusH = 28;
    m_bar.SetRect(m_rect.x, m_rect.y, m_rect.w, m_menuH);
    const s32 ay = m_rect.y + m_menuH;
    const s32 ah = m_rect.h - m_menuH - m_statusH;
    if(m_viewRoll) {
        // Вид-сетка на всё поле: 36 строк не разложить ещё и над клавиатурой
        // (при обычной высоте окна строка будет ~10 px и кликом не угадаешь).
        m_roll.SetRect(m_rect.x, ay, m_rect.w, ah);
        return;
    }
    // «16 каналов»: клавиатура — треть снизу (не меньше 110), остальное — время.
    s32 pianoH = ah * 35 / 100;
    if(pianoH < 110) pianoH = 110;
    if(ah - pianoH < 200) pianoH = ah - 200;
    if(pianoH < 90) pianoH = (ah > 90) ? 90 : ah;
    const s32 tlH = ah - pianoH;
    if(tlH > 0) m_tlView.SetRect(m_rect.x, ay, m_rect.w, tlH);
    if(pianoH > 0) m_piano.SetRect(m_rect.x, ay + tlH, m_rect.w, pianoH);
}

// ── СЛУЖЕБНЫЕ КОЛБЭКИ ───────────────────────────────────────────────────
// Колбэки — ОДНОУРОВНЕВЫЕ ФУНКЦИИ: виджеты не знают ни про AudioBus,
// ни про ленту; экран кладёт свой указатель в user и получает событие.
void SmpScreen::noteHandler(s32 on, s32 midi, void* user) {
    ((SmpScreen*)user)->onNote(on, midi);
}

void SmpScreen::placeHandler(s32 row, s32 beat, void* user) {
    ((SmpScreen*)user)->onPlace(row, beat);
}

void SmpScreen::onNote(s32 on, s32 midi) {
    if(on) {
        m_bus->NoteOn(midi, 100, (u32)m_wave);
        m_lastNote = midi;
        // Режим вставки: та же нотация, нота уходит в ленту, маркер — шаг.
        if(m_insertMode) insertAtMarker(midi);
    }
    else   m_bus->NoteOff(midi);
}

void SmpScreen::onPlace(s32 row, s32 beat) {
    m_markerRow = row;
    m_markerBeat = beat;
}

void SmpScreen::insertAtMarker(s32 midi) {
    // Маркера ещё не было (ещё не кликнули по времени) — берём точку (0,0):
    // кликнуть «в никуда» нельзя, а молча бросать ноту — тем более.
    if(m_markerRow < 0) { m_markerRow = 0; m_markerBeat = 0; }
    m_tl.Insert(m_markerRow, m_markerBeat, midi, (u32)m_wave);
    if(m_markerBeat + 1 < Timeline::kBeats) m_markerBeat++;
}

void SmpScreen::SetView(s32 roll) {
    m_viewRoll = (roll != 0);
    // Видимость — целиком: невидимый вид не рисует и не принимает клики.
    m_roll.SetVisible(m_viewRoll);
    m_roll.SetActive(m_viewRoll);
    m_tlView.SetVisible(!m_viewRoll);
    m_tlView.SetActive(!m_viewRoll);
    m_piano.SetVisible(!m_viewRoll);
    m_piano.SetActive(!m_viewRoll);
    layout();
}

// ── ВИД-СЕТКА «КЛАВИШИ» ──────────────────────────────────────────────────
// Строка сетки = клавиша: нота клетки — всегда частота её строки. Что
// ставить как — решает здесь, виджет только докладывает координаты.
void SmpScreen::onRollNote(s32 on, s32 midi) {
    // Вертикальная клавиша — прослушивание тона (живой звук), а не вставка:
    // клетку человек ставит по сетке, клавишей он подбирает ухо.
    if(on) m_bus->NoteOn(midi, 100, (u32)m_wave);
    else   m_bus->NoteOff(midi);
}

void SmpScreen::previewNote(s32 midi) {
    m_bus->Blip(midi, (u32)m_wave, 0.08f);   // короткий тон: слышно, какую вставил
}

void SmpScreen::onRollCell(s32 row, s32 beat, bool ctrl) {
    // Переключатель: клетка занята — снять (штрих надвое, клетка — паузой),
    // пуста — вставить (+ превью). Ctrl — клетка-стаккато: короткая нота со
    // своей паузой в доле.
    const s32 midi = Timeline::RowMidi(row);
    if(m_tl.HasAny(row, beat, midi)) {
        m_tl.RemoveCell(row, beat, midi);
        m_lastNote = midi;
        return;
    }
    if(m_tl.Insert(row, beat, midi, (u32)m_wave, ctrl)) {
        previewNote(midi);
        m_lastNote = midi;
    }
}

// Штрих — ОДНА нота длиной lo..hi (слово владельца 16.09.2026: «сплошной
// звук ноты... сплошным горизонтальным прямоугольником»): в проигрывании
// один «вкл» в начале, один «выкл» в конце — звук не рвётся на границах
// клеток; в сетке — единый прямоугольник. Клетка занята — false.
bool SmpScreen::paintRange(s32 row, s32 lo, s32 hi) {
    if(hi < lo) { s32 t = lo; lo = hi; hi = t; }
    const s32 midi = Timeline::RowMidi(row);
    if(!m_tl.Paint(row, lo, hi, midi, (u32)m_wave)) return false;
    m_lastNote = midi;
    return true;
}

void SmpScreen::onRollPaint(s32 row, s32 lo, s32 hi) {
    paintRange(row, lo, hi);
    // Живая протяжка: пока штрих рисуется — один СПЛОШНОЙ тон этой высоты
    // (не превью-трек на каждую клетку); отпускается в onRollPaintEnd.
    const s32 midi = Timeline::RowMidi(row);
    if(m_rollSustain != midi) {
        if(m_rollSustain >= 0) m_bus->NoteOff(m_rollSustain);
        m_bus->NoteOn(midi, 100, (u32)m_wave);
        m_rollSustain = midi;
    }
}

void SmpScreen::onRollPaintEnd(s32 row) {
    const s32 midi = Timeline::RowMidi(row);
    if(m_rollSustain == midi) {
        m_bus->NoteOff(midi);
        m_rollSustain = -1;
    }
}

void SmpScreen::onRollErase(s32 row, s32 beat) {
    m_tl.RemoveCell(row, beat, Timeline::RowMidi(row));
}

void SmpScreen::rollNoteHandler(s32 on, s32 midi, void* user) {
    ((SmpScreen*)user)->onRollNote(on, midi);
}

void SmpScreen::rollCellHandler(s32 row, s32 beat, bool ctrl, void* user) {
    ((SmpScreen*)user)->onRollCell(row, beat, ctrl);
}

void SmpScreen::rollPaintHandler(s32 row, s32 lo, s32 hi, void* user) {
    ((SmpScreen*)user)->onRollPaint(row, lo, hi);
}

void SmpScreen::rollPaintEndHandler(s32 row, void* user) {
    ((SmpScreen*)user)->onRollPaintEnd(row);
}

void SmpScreen::rollEraseHandler(s32 row, s32 beat, void* user) {
    ((SmpScreen*)user)->onRollErase(row, beat);
}

void SmpScreen::ToggleRecord() {
    const s32 st = m_bus->RecState();
    if(st == AudioBus::kRecOn || st == AudioBus::kRecSaving) {
        m_bus->StopRecord();
        return;
    }
    if(!m_recordDir[0]) return;         // каталога нет — запись невозможна

    char name[64];
    char full[512];
    std::time_t now = std::time(nullptr);
    std::tm tmv;
    localtime_r(&now, &tmv);
    // Имя по времени записи: без диалога ввода имени — отдельное окно под
    // текстовое поле для одного имени не заводим (поиск F-9010: текст в
    // окне приложения идёт в отдельном окне, а здесь оно не окупает).
    std::snprintf(name, sizeof name, "synth_%04d%02d%02d_%02d%02d%02d.wav",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    std::snprintf(full, sizeof full, "%s/%s", m_recordDir, name);
    m_bus->StartRecord(full);
}

void SmpScreen::onMenu(const char* commandId) {
    if(!commandId) return;
    if(!std::strncmp(commandId, "wave.", 5)) {
        m_wave = std::atoi(commandId + 5);
        m_bus->SetWave((u32)m_wave);
        return;
    }
    if(!std::strncmp(commandId, "vol.", 4)) {
        m_volumePct = std::atoi(commandId + 4);
        m_bus->SetVolume((f32)m_volumePct / 100.0f);
        return;
    }
    if(!std::strcmp(commandId, "play")) { m_tl.Play(); return; }
    if(!std::strcmp(commandId, "stop")) { m_tl.Stop(); return; }
    if(!std::strcmp(commandId, "ins.toggle")) { m_insertMode = !m_insertMode; return; }
    if(!std::strcmp(commandId, "clear.tape")) { m_tl.Clear(); return; }
    if(!std::strcmp(commandId, "metro.on")) { m_tl.SetMetro(!m_tl.Metro()); return; }
    if(!std::strncmp(commandId, "bpm.", 4)) { m_tl.SetBpm((f32)std::atoi(commandId + 4)); return; }
    if(!std::strcmp(commandId, "rec.toggle")) {
        ToggleRecord();
        return;
    }
    if(!std::strcmp(commandId, "view.tracks")) { SetView(0); return; }
    if(!std::strcmp(commandId, "view.roll")) { SetView(1); return; }
    if(!std::strcmp(commandId, "instr.cfg")) { openInstrDlg(); return; }
    if(!std::strcmp(commandId, "env.edit"))  { openEnvDlg();  return; }
    if(!std::strcmp(commandId, "quit")) {
        m_quit = true;
    }
}

// ── ЖИВОЙ КАНАЛ (AppAgent) ───────────────────────────────────────────────
// Команды канала зовут те же методы, что и меню — одно действие в одном месте.
void SmpScreen::AgentNoteOn(s32 midi)  { onNote(1, midi); }
void SmpScreen::AgentNoteOff(s32 midi) { onNote(0, midi); }

void SmpScreen::AgentWave(s32 wave) {
    if(wave < 0 || wave >= (s32)SynthEngine::WaveCount) return;
    m_wave = wave;
    m_bus->SetWave((u32)m_wave);
}

void SmpScreen::AgentVolume(s32 volumePct) {
    if(volumePct < 5 || volumePct > 100) return;
    m_volumePct = volumePct;
    m_bus->SetVolume((f32)volumePct / 100.0f);
}

void SmpScreen::AgentRecord(bool start) {
    const s32 st = m_bus->RecState();
    if(start) {
        if(st == AudioBus::kRecIdle) ToggleRecord();
    } else if(st == AudioBus::kRecOn || st == AudioBus::kRecSaving) {
        m_bus->StopRecord();
    }
}

void SmpScreen::AgentPlay()  { m_tl.Play(); }
void SmpScreen::AgentStop()  { m_tl.Stop(); }

void SmpScreen::AgentInsert(s32 row, s32 beat, s32 midi) {
    if(!m_tl.Insert(row, beat, midi, (u32)m_wave)) return;
    // Если вставка пришлась в точку текущего маркера — маркер сдвинут,
    // как и в живой клавиатурной вставке (одна семантика в одном месте).
    if(m_insertMode && row == m_markerRow && beat == m_markerBeat)
        if(m_markerBeat + 1 < Timeline::kBeats) m_markerBeat++;
}

void SmpScreen::AgentClear() { m_tl.Clear(); }

void SmpScreen::AgentMetro(bool on) { m_tl.SetMetro(on); }

void SmpScreen::AgentBpm(s32 bpm) {
    if(bpm < 30 || bpm > 240) return;
    m_tl.SetBpm((f32)bpm);
}

void SmpScreen::AgentInsertMode(bool on) { m_insertMode = on; }

void SmpScreen::AgentView(bool roll) { SetView(roll ? 1 : 0); }

void SmpScreen::AgentCell(s32 row, s32 beat) { onRollCell(row, beat, false); }
void SmpScreen::AgentCellCtrl(s32 row, s32 beat) { onRollCell(row, beat, true); }

void SmpScreen::AgentPaint(s32 row, s32 lo, s32 hi) {
    // Канал не видит мышь — живого тона нет; короткий превью вместо него.
    const s32 midi = Timeline::RowMidi(row);
    if(paintRange(row, lo, hi)) previewNote(midi);
}

void SmpScreen::AgentPreview(s32 midi) {
    if(midi < 0 || midi > 127) return;
    // Прогон огибающей на слух: тон идёт на всё окно (2,0 с) + хвост.
    m_bus->Blip(midi, (u32)m_wave, 2.2f);
}

void SmpScreen::AgentEnv(char* reply, s32 replySize) {
    if(replySize <= 0) return;
    const s32 n = m_instr.envN;
    if(n < 1) {
        std::snprintf(reply, (size_t)replySize, "огибающая: нет точек");
        return;
    }
    s32 pi = 0;
    for(s32 i = 1; i < n; ++i)
        if(m_instr.envV[i] > m_instr.envV[pi]) pi = i;
    std::snprintf(reply, (size_t)replySize,
                  "огибающая: окно %.1f с, %d т. · пик %.2f (t=%.2f с) · край %.2f",
                  (f32)SynthEngine::kEnvWindow, n,
                  m_instr.envV[pi], m_instr.envT[pi], m_instr.envV[n - 1]);
}

void SmpScreen::AgentLineStats(char* reply, s32 replySize) {
    m_bus->LineStats(reply, replySize);
}

void SmpScreen::AgentNotes(char* reply, s32 replySize) {
    char* p = reply;
    const s32 left = replySize - (s32)std::strlen(reply);
    *p = 0;
    Timeline::Note sn[Timeline::kMaxNote];
    const s32 n = m_tl.CopyNotes(sn, Timeline::kMaxNote);
    if(n == 0) {
        std::snprintf(p, (size_t)left, "лента пуста");
        return;
    }
    p += std::snprintf(p, (size_t)left, "нот: %d", n);
    for(s32 i = 0; i < n && p - reply < replySize - 4; ++i) {
        const s32 left = replySize - (s32)(p - reply);
        if(sn[i].dur > 1)   // xN — штрих (одна нота длиной N)
            p += std::snprintf(p, (size_t)left, " | %d:%d=%d[%d]x%d",
                               sn[i].row, sn[i].beat, sn[i].midi, sn[i].wave, sn[i].dur);
        else
            p += std::snprintf(p, (size_t)left, " | %d:%d=%d[%d]",
                               sn[i].row, sn[i].beat, sn[i].midi, sn[i].wave);
    }
}

const char* SmpScreen::StatusText() {
    // ОДНА формулировка на глаз (Draw) и на ответ живому каналу: канал обязан
    // видеть экран, а не собственную версию правды.
    char rec[480];
    const s32 st = m_bus->RecState();
    if(st == AudioBus::kRecOn) {
        std::snprintf(rec, sizeof rec, "  ·  Запись: %.1f с", m_bus->RecSeconds());
    } else if(st == AudioBus::kRecSaving) {
        std::snprintf(rec, sizeof rec, "  ·  Запись: сохранение...");
    } else {
        char last[384];
        m_bus->LastRecordPath(last, (s32)sizeof last);
        if(last[0]) std::snprintf(rec, sizeof rec, "  ·  Записано: %s", last);
        else        rec[0] = 0;
    }

    char note[48];
    note[0] = 0;
    if(m_lastNote >= 0) {
        char name[16];
        PianoView::NoteName(m_lastNote, name, (s32)sizeof name);
        std::snprintf(note, sizeof note, "  ·  Нота: %s", name);
    }

    char tr[96];
    if(m_tl.Playing())
        std::snprintf(tr, sizeof tr, "  ·  Идёт: доля %d/%d",
                      (s32)m_tl.PosBeats(), Timeline::kBeats);
    else if(m_tl.NoteCount() > 0)
        std::snprintf(tr, sizeof tr, "  ·  Лента: %d нот(ы)", m_tl.NoteCount());
    else
        tr[0] = 0;

    char ins[96];
    if(m_insertMode) {
        if(m_markerRow >= 0)
            std::snprintf(ins, sizeof ins, "  ·  Вставка: вкл  Курс: %d:%d",
                          m_markerRow + 1, m_markerBeat + 1);
        else
            std::snprintf(ins, sizeof ins, "  ·  Вставка: вкл  Курс: —");
    } else {
        ins[0] = 0;
    }

    std::snprintf(m_status, sizeof m_status,
                  "Инструмент: %s · Волна: %s%s  ·  Вид: %s  ·  Громкость: %d %%  ·  Голоса: %d/%d  ·  Темп: %d %s%s%s",
                  m_instr.name, SynthEngine::WaveName((SynthEngine::WaveKind)m_wave), note,
                  m_viewRoll ? "клавиши" : "каналы",
                  m_volumePct, m_bus->ActiveVoices(), SynthEngine::kVoices,
                  (s32)m_tl.Bpm(), tr, ins, rec);
    return m_status;
}

void SmpScreen::Draw(HID_context& ctx) {
    if(!IsVisible()) return;
    HID_box::Draw(ctx);   // фон

    // Галки пунктов-переключателей — фактическое состояние, а не память
    // прошлого клика (паттерн CFD syncMenuChecks).
    static const char* waveIds[4] = { "wave.0", "wave.1", "wave.2", "wave.3" };
    for(s32 w = 0; w < 4; ++w) m_mWave.SetChecked(waveIds[w], w == m_wave);
    static const char* volIds[10] = { "vol.10", "vol.20", "vol.30", "vol.40", "vol.50",
                                     "vol.60", "vol.70", "vol.80", "vol.90", "vol.100" };
    for(s32 i = 0; i < 10; ++i) m_mVol.SetChecked(volIds[i], (i + 1) * 10 == m_volumePct);
    m_mTr.SetChecked("ins.toggle", m_insertMode);
    m_mView.SetChecked("view.tracks", !m_viewRoll);
    m_mView.SetChecked("view.roll", m_viewRoll);
    m_mMetro.SetChecked("metro.on", m_tl.Metro());
    const s32 bpmNow = (s32)(m_tl.Bpm() + 0.5f);
    for(s32 b = 60; b <= 180; b += 10) {
        char id[16];
        std::snprintf(id, sizeof id, "bpm.%d", b);
        m_mMetro.SetChecked(id, b == bpmNow);
    }

    // Подсветка клавиатур — из фактического состояния голосов (каждый кадр:
    // нота, сыгранная живым каналом или лентой, должна гореть).
    {
        s32 notes[SynthEngine::kVoices + 1];
        m_bus->ActiveNotes(notes);
        m_piano.SetActiveNotes(notes, SynthEngine::kVoices);
        m_roll.SetActiveNotes(notes, SynthEngine::kVoices);
    }
    // Маркер вставки виден, только когда вставка включена: иначе он врет.
    m_tlView.SetCursor(m_insertMode ? m_markerRow : -1, m_insertMode ? m_markerBeat : -1);
    // Не видимые виды сами не рисуются (проверяют IsVisible) — зовем всех.
    m_tlView.Draw(ctx);
    m_roll.Draw(ctx);
    m_piano.Draw(ctx);

    // Строка состояния.
    const s32 sy = m_rect.y + m_rect.h - m_statusH;
    HID_fill(ctx, m_rect.x, sy, m_rect.w, m_statusH, 30, 30, 38);
    HID_drawText(ctx, m_rect.x + 8, sy + 5, StatusText(), 200, 200, 212);

    // Диалоги инструмента — модально по центру (паттерн FEMM): форма
    // маленькая, редактор кривой — крупнее (место под график).
    if(m_instrDlg.IsVisible()) {
        s32 w = m_rect.w - 150; if(w > 520) w = 520; if(w < 360) w = 360;
        s32 h = m_rect.h - 140; if(h > 240) h = 240; if(h < 150) h = 150;
        m_instrDlg.SetRect(m_rect.x + (m_rect.w - w) / 2, m_rect.y + (m_rect.h - h) / 2, w, h);
        m_instrDlg.Draw(ctx);
    }
    if(m_envDlg.IsVisible()) {
        s32 w = m_rect.w - 100; if(w > 900) w = 900; if(w < 420) w = 420;
        s32 h = m_rect.h - 90;  if(h > 560) h = 560; if(h < 300) h = 300;
        m_envDlg.SetRect(m_rect.x + (m_rect.w - w) / 2, m_rect.y + (m_rect.h - h) / 2, w, h);
        m_envDlg.Draw(ctx);
    }

    // Меню последним: выпадающие списки поверх всего.
    m_bar.LayoutMenus(ctx);
    m_bar.Draw(ctx);
}

void SmpScreen::ProcessEvent(const HID_event& event, bool& handled) {
    if(!IsActive() || !IsVisible()) return;

    // Диалоги инструмента перехватывают ВСЁ, пока открыты (паттерн FEMM):
    // меню и виды под ними не должны щёлкать «сквозь» модальное окно.
    if(m_instrDlg.IsVisible()) {
        m_instrDlg.ProcessEvent(event, handled);
        if(m_instrDlg.TakeAccepted()) {
            m_instrDlg.Close();
            applyInstrDlg();
        }
        handled = true;
        return;
    }
    if(m_envDlg.IsVisible()) {
        m_envDlg.ProcessEvent(event, handled);
        if(m_envDlg.TakeAccepted()) {
            m_envDlg.Close();
            applyEnvDlg();
        }
        handled = true;
        return;
    }

    // Меню первыми: навигация, клики по пунктам, MenuFocus.
    m_bar.ProcessEvent(event, handled);
    const char* cmd = m_bar.TakeMenuCommandId();
    if(cmd) { onMenu(cmd); handled = true; return; }
    if(handled) return;

    // Каждый виджет принимает свою часть окна (PointerMove — всем:
    // у каждого свой прицел, и он не мешает другому; невидимые сами
    // отклоняют события — IsActive/IsVisible).
    if(m_roll.Contains(event.x, event.y) || event.type == HID_event_type::PointerMove) {
        m_roll.ProcessEvent(event, handled);
        if(handled) return;
    }
    if(m_tlView.Contains(event.x, event.y) || event.type == HID_event_type::PointerMove) {
        m_tlView.ProcessEvent(event, handled);
        if(handled) return;
    }
    if(m_piano.Contains(event.x, event.y) || event.type == HID_event_type::PointerMove) {
        m_piano.ProcessEvent(event, handled);
        if(handled) return;
    }
}

}  // namespace Smp
