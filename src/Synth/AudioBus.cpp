#include "AudioBus.hpp"
#include "SynthEngine.hpp"
#include "Audio/Audio.hpp"
#include "Audio/Wav.hpp"

#include <SDL.h>
#include <cstring>

namespace Smp {

AudioBus::AudioBus(SynthEngine* engine) : m_engine(engine) {
    std::memset(m_notes, 0, sizeof m_notes);
    for(int i = 0; i < SynthEngine::kVoices; ++i) m_notes[i] = -1;
    m_notes[SynthEngine::kVoices] = -1;
}

AudioBus::~AudioBus() {
    Stop();
}

bool AudioBus::Start() {
    if(m_run.load()) return true;
    if(!m_dev.Init(48000)) return false;      // нет устройства — молчим честно
    m_sink = m_dev.OpenSink(0, kRing);       // шина 0 = «мир/музыка»
    if(!m_sink) { m_dev.Shutdown(); return false; }
    // Маржа линии: досыпать молчание ДО запуска — уровень кольца равен
    // задержке «нажато→услышано», и он же — запас на системные просадки:
    // пока уровень выше нуля, устройство не услышит ни одного нуля.
    // Досыпаем один такт (2048): основное наполнение придёт само —
    // прайминг устройства кладёт в кольцо 3–14 к отсчётов (замер
    // 16.09.2026), и кольцо на 32768 держит это с запасом до дна и до
    // верха. Больше досыпать незачем: каждый отсчёт префилла — чистая
    // задержка нажатия до звука.
    {
        Real zeros[kChunk] = {0};
        m_sink->Write(zeros, kChunk);
    }
    m_paceFreq = SDL_GetPerformanceFrequency();
    if(m_paceFreq == 0) m_paceFreq = 1;
    m_paceNext = nowPerfMs();
    m_lineStartMs.store(SDL_GetTicks(), std::memory_order_relaxed);
    // prefill в счёт не идёт: темп — то, что производит ПОТОК, после старта.
    m_lineWritten.store(0, std::memory_order_relaxed);
    m_minPend.store(kRing, std::memory_order_relaxed);
    m_run.store(true);
    m_thread = std::thread([this] { loop(); });
    return true;
}

void AudioBus::Stop() {
    if(!m_run.load() && !m_thread.joinable()) return;
    m_run.store(false);
    // Поток сам дослушает текущий такт: в нем он проверяет флаг после цикла.
    if(m_thread.joinable()) m_thread.join();
    if(m_sink) { m_dev.CloseSink(m_sink); m_sink = nullptr; }
    m_dev.Shutdown();
}

void AudioBus::NoteOn(s32 midi, s32 velocity, u32 wave) {
    Event e{ kEvtNoteOn, midi, velocity, (s32)wave };
    pushEvent(e);
}

void AudioBus::NoteOff(s32 midi) {
    Event e{ kEvtNoteOff, midi, 0, 0 };
    pushEvent(e);
}

void AudioBus::Blip(s32 midi, u32 wave, f32 durSec) {
    s32 frames = (s32)(durSec * 48000.0f);
    if(frames < 480) frames = 480;       // короче 10 мс тон не слышно
    if(frames > 120000) frames = 120000; // превью — не дольше 2,5 с (окно огибающей + хвост)
    Event e{ kEvtBlip, midi, frames, (s32)wave };
    pushEvent(e);
}

void AudioBus::SetWave(u32 wave) { m_wave.store(wave); }

void AudioBus::SetEnvelope(const f32* lut) { m_engine->SetEnvelope(lut); }
void AudioBus::SetVolume(f32 v) { m_vol.store(v); }

void AudioBus::ActiveNotes(s32* out) const {
    std::lock_guard<std::mutex> lk(m_notesMu);
    for(int i = 0; i <= SynthEngine::kVoices; ++i) out[i] = m_notes[i];
}

f32 AudioBus::RecSeconds() const {
    return m_recFrames.load() / 48000.0f;
}

void AudioBus::LastRecordPath(char* buf, s32 size) const {
    std::lock_guard<std::mutex> lk(m_recMu);
    if(size > 0) std::snprintf(buf, (size_t)size, "%s", m_lastPath);
}

bool AudioBus::StartRecord(const char* filePath) {
    if(m_recState.load() != kRecIdle) return false;
    m_recBuf.clear();
    m_recBuf.reserve(kMaxRecFrames);
    std::snprintf(m_recPath, sizeof m_recPath, "%s", filePath);
    m_recFrames.store(0);
    m_recState.store(kRecOn);
    return true;
}

void AudioBus::StopRecord() {
    if(m_recState.load() == kRecOn) m_recState.store(kRecSaving);
}

void AudioBus::saveRecording() {
    // Тело — в звуковом потоке: отсчёты у него под боком, и никакого
    // блокирующего копирования из него не требуется.
    const s32 frames = (s32)m_recFrames.load();
    bool ok = frames > 0 && Audio::Wav::Save(m_recPath, m_recBuf.data(), frames, 48000);
    if(ok) {
        std::lock_guard<std::mutex> lk(m_recMu);
        std::snprintf(m_lastPath, sizeof m_lastPath, "%s", m_recPath);
    }
    m_recBuf.clear();
    m_recState.store(kRecIdle);
}

bool AudioBus::pushEvent(const Event& e) {
    const s32 h = m_evtHead.load(std::memory_order_relaxed);
    const s32 t = m_evtTail.load(std::memory_order_acquire);
    if(h - t >= kEvtCap) return false;        // очередь пуста быть не должна: 64 нот
    m_evt[h & kEvtMask] = e;
    m_evtHead.store(h + 1, std::memory_order_release);
    return true;
}

bool AudioBus::popEvent(Event& e) {
    const s32 t = m_evtTail.load(std::memory_order_relaxed);
    const s32 h = m_evtHead.load(std::memory_order_acquire);
    if(t >= h) return false;
    e = m_evt[t & kEvtMask];
    m_evtTail.store(t + 1, std::memory_order_release);
    return true;
}

f64 AudioBus::nowPerfMs() const {
    return (f64)SDL_GetPerformanceCounter() * 1000.0 / (f64)m_paceFreq;
}

void AudioBus::loop() {
    while(true) {
        if(!m_run.load()) break;

        // Лента времени: за этот такт (доли считаются из фреймов и темпа)
        // могли наступить ноты и доли метронома. Выстрелы исполняют в
        // ПОЯВЛЯЮЩЕЙСЯ последовательности — раньше живых нот, чтобы клик
        // не потерялся за огибающей вставленной ноты.
        // Отложенные выстрелы (Fired.chunks — «выкл» стаккато): досчитанные
        // исполняем. Очередь ведёт только звуковой поток — без замка;
        // исполненный элемент замещается хвостом (все отсрочки одного
        // вида, их взаимный порядок значения не имеет).
        for(s32 i = 0; i < m_delayedN; ) {
            if(m_delayed[i].wait > 0) { --m_delayed[i].wait; ++i; continue; }
            const Timeline::Fired f = m_delayed[i].f;
            m_delayed[i] = m_delayed[m_delayedN - 1];
            --m_delayedN;
            if(f.kind == 0)
                m_engine->NoteOn(f.midi, 100, (SynthEngine::WaveKind)f.wave);
            else if(f.kind == 1)
                m_engine->NoteOff(f.midi);
        }

        if(m_tl) {
            const f32 dtBeats = (f32)kChunk * (m_tl->Bpm() / 60.0f) / 48000.0f;
            const s32 nf = m_tl->Step(dtBeats, m_fired, 128);
            for(s32 i = 0; i < nf; ++i) {
                const Timeline::Fired& f = m_fired[i];
                if(f.chunks > 0) {
                    // Отсрочка: в очередь, счёт пойдёт отсюда, чанк за чанком.
                    // Лента не знает миллисекунд — рендер-такт её атом времени.
                    if(m_delayedN < kDelayCap) {
                        m_delayed[m_delayedN].f = f;
                        m_delayed[m_delayedN].wait = f.chunks;
                        ++m_delayedN;
                    }
                    continue;
                }
                if(f.kind == 0)
                    m_engine->NoteOn(f.midi, 100, (SynthEngine::WaveKind)f.wave);
                else if(f.kind == 1)
                    m_engine->NoteOff(f.midi);
                else {
                    // Щелчок — одноразовый тон микшера (не запись: он живёт
                    // вне дорожки синтеза и в WAV не попадает — по делу).
                    const f32 fr = (f.kind == 3) ? 880.0f : 660.0f;
                    const f32 amp = (f.kind == 3) ? 0.22f : 0.13f;
                    m_dev.Beep(fr, fr, 0.05f, amp, Audio::Wave::Sine, 0.0f, 0);
                }
            }
        }


        // Ноты текущего такта — до рендера: задержка от щелчка до звука
        // не превосходит такта (43 мс), а не наоборот.
        Event e;
        while(popEvent(e)) {
            if(e.type == kEvtNoteOn)
                m_engine->NoteOn(e.midi, e.vel, (SynthEngine::WaveKind)e.wave);
            else if(e.type == kEvtNoteOff)
                m_engine->NoteOff(e.midi);
            else {
                // kEvtBlip: превью-тон — ноту включаем, vel — сколько ОТСЧЁТОВ
                // он ещё должен звучать; отпускание — ниже, по отсчёту.
                m_engine->NoteOn(e.midi, 100, (SynthEngine::WaveKind)e.wave);
                m_blipMidi = e.midi;
                m_blipFrames = (u32)e.vel;
            }
        }
        if(m_blipMidi >= 0) {
            if(m_blipFrames >= (u32)kChunk) m_blipFrames -= (u32)kChunk;
            else { m_engine->NoteOff(m_blipMidi); m_blipMidi = -1; }
        }

        Real buf[kChunk];
        m_engine->Render(buf, kChunk, m_vol.load());

        // Запись: те же отсчёты, что ушли в микшер — одна лента, два потребителя.
        if(m_recState.load() == kRecOn) {
            u32 done = m_recFrames.load();
            if(done >= (u32)kMaxRecFrames) {
                StopRecord();                 // потолок: сохранить и замолчать
            } else {
                s32 left = kMaxRecFrames - (s32)done;
                if(left > kChunk) left = kChunk;
                // Пишем в ЗАРЕЗЕРВИРОВАННУЮ полосу напрямую (data, а не operator[]):
                // счётчик count остаётся нулевым, пока запись не сохранена —
                // иначе файл всё равно пришлось бы копировать.
                Real* d = m_recBuf.data();
                for(s32 i = 0; i < left; ++i) d[done + i] = buf[i];
                m_recFrames.store(done + (u32)left);
            }
        }
        if(m_recState.load() == kRecSaving) saveRecording();

        // Вход мог быть полон — не влезший хвост такта сбрасывается, и это
        // слышно (разрыв тона). Слышны и нули, когда кольцо голодает. Оба
        // события — брак линии: считаем, чтобы принимали по числам, а не на слух.
        // Кольцо было ПУСТЫМ в начале такта — устройство в этом такте
        // услышало нули: это и есть тот самый «щелчок», только по счёту.
        const s32 pend0 = m_sink->Pending();
        const s32 took = m_sink->Write(buf, kChunk);
        const s32 pend = m_sink->Pending();
        m_lineWritten.fetch_add((u64)took, std::memory_order_relaxed);
        if(pend0 <= 0) m_gapEv.fetch_add(1, std::memory_order_relaxed);
        if(took < kChunk) {
            m_dropEv.fetch_add(1, std::memory_order_relaxed);
            m_lastPend.store(pend, std::memory_order_relaxed);
        } else if(pend0 <= 0) {
            m_lastPend.store(0, std::memory_order_relaxed);
        }
        if(took < kChunk || pend0 <= 0)
            m_lastMs.store(SDL_GetTicks(), std::memory_order_relaxed);
        if(pend < m_minPend.load(std::memory_order_relaxed))
            m_minPend.store(pend, std::memory_order_relaxed);
        m_voices.store(m_engine->ActiveVoices());
        {
            std::lock_guard<std::mutex> lk(m_notesMu);
            m_engine->ActiveNotes(m_notes);
        }

        // Темп потока — СЕТКА ДЕДЛАЙНОВ, а не «сон после такта». Сетка: i-й
        // такт положен в кольцо в момент i·42,667 мс (performance-часы).
        // До дедлайна спим, и всё: не доспал на микросекунды (округление
        // SDL_Delay) — следующего такта дождёмся, переспал — следующий
        // сон короче. Рывок прошлого компенсируется самим дедлайном, и
        // средний темп = ровно 48000 Гц при любом пересыпе таймера.
        // ВАЖНО: сетку подтаскивать к «сейчас» нельзя, если опоздание ≤ 1 мс,
        // — подтаскивание смещает дедлайн ВПЕРЁД на некомпенсированную долю,
        // и средний темп уходит вверх (замер 16.09.2026: +22 Гц). Сбрасывать
        // сетку — только на ступор больше миллисекунды: там аудио потеряно
        // физически и догонять рывком нельзя.
        m_paceNext += (f64)kChunk * 1000.0 / 48000.0;
        const f64 nowMs = nowPerfMs();
        if(m_paceNext > nowMs) SDL_Delay((u32)(m_paceNext - nowMs));
        if(m_paceNext < nowPerfMs() - 1.0) m_paceNext = nowPerfMs();
    }
    // После выхода: если запись так и не сохранили (выход приложения по
    // середине), файл всё равно дописывается — «записать и закрыть» не теряет
    // то, что уже записал.
    if(m_recState.load() == kRecOn || m_recState.load() == kRecSaving) saveRecording();
}

void AudioBus::LineStats(char* reply, s32 size) const {
    if(size <= 0) return;
    // Темп производителя: сколько ГЕРЦ он положил в кольцо за время линии.
    // Должен сходиться к 48000: меньше — оседание (нулевые такты), больше —
    // переполнение (сброшенные хвосты). Паспорт тактирования — по числу.
    f64 rate = 0.0;
    const u32 span = SDL_GetTicks() - m_lineStartMs.load(std::memory_order_relaxed);
    if(span >= 1000)
        rate = (f64)m_lineWritten.load(std::memory_order_relaxed) * 1000.0 / (f64)span;
    std::snprintf(reply, (size_t)size,
                  "линия: t %u мс · уровень %d · минимум %d · темп %.1f Гц · написано %llu · срывов %llu · голодов %llu · посл. %d @ %u мс",
                  SDL_GetTicks(),
                  m_sink ? m_sink->Pending() : 0,
                  m_minPend.load(std::memory_order_relaxed),
                  rate,
                  (unsigned long long)m_lineWritten.load(std::memory_order_relaxed),
                  (unsigned long long)m_dropEv.load(std::memory_order_relaxed),
                  (unsigned long long)m_gapEv.load(std::memory_order_relaxed),
                  m_lastPend.load(std::memory_order_relaxed),
                  (unsigned)m_lastMs.load(std::memory_order_relaxed));
}

}  // namespace Smp
