#pragma once
#include "Core/Types.hpp"

#include "AppFramework/AppRuntimePaths.hpp"
#include "Synth/Instrument.hpp"

// Настройки SymphoniaMechanica: <каталог_бинаря>/.smp/settings.cfg (INI)
// средствами общей библиотеки (AppRuntimePaths + ConfigFile).
struct SmpSettings {
    s32 windowWidth = 1120;
    s32 windowHeight = 600;
    s32 wave = 0;          // Smp::SynthEngine::WaveKind
    s32 volume = 55;       // 0..100
    s32 bpm = 120;         // темп метронома, долей в минуту
    s32 metro = 1;         // метроном вкл/выкл
    s32 insert = 0;        // режим вставки нот (по умолчанию жилая игра)
    s32 view = 0;          // 0 — «16 каналов», 1 — вид-сетка «Клавиши»
    // Инструмент: форма сигнала + огибающая (окно 2,0 с, до 32 точек).
    // Старт — заводской; Load подменяет секцией [Instrument], если она есть.
    Smp::Instrument instrument;

    bool Init(const char* argv0);
    void Load();
    void Save() const;

    const char* SettingsPath() const { return m_paths.SettingsFile().c_str(); }
    const char* FontFile() const     { return m_paths.DefaultFontFile().c_str(); }

    // Каталог записей WAV: .smp/recordings/ (рядом с настройками, локальный).
    const char* RecordingsDir() const;
    bool RecordingsReady() const { return m_recOk; }

private:
    AppRuntimePaths m_paths;
    bool m_recOk = false;
};
