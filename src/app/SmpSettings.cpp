#include "SmpSettings.hpp"
#include "AppConfig/ConfigFile.hpp"
#include "Core/String.hpp"

#include <cstdio>
#include <cstring>

bool SmpSettings::Init(const char* argv0) {
    // Имя каталога = имя бинарника («smp») — правило мастерской (см. разбор
    // AppRuntimePaths: бинарник в корне, каталог с тем же именем невозможен,
    // библиотека решит сама — уйдёт в ».smp/« и скажет об этом в Note()).
    const bool ok = m_paths.Init(argv0 ? argv0 : "smp", "smp");
    String dir;
    m_paths.BuildConfigDirectoryPath(dir, "recordings");
    m_recOk = m_paths.EnsureConfigDirectory("recordings");
    return ok;
}

void SmpSettings::Load() {
    instrument = Smp::InstrumentLib::At(0);   // заводской — до прочтения файла
    ConfigFile cfg;
    if(!cfg.Load(m_paths.SettingsFile().c_str())) {
        Save();   // файла ещё нет — положить значения по умолчанию
        return;
    }
    windowWidth  = cfg.GetInt("Window", "Width",  windowWidth);
    windowHeight = cfg.GetInt("Window", "Height", windowHeight);
    wave         = cfg.GetInt("Sound", "Wave",   wave);
    volume       = cfg.GetInt("Sound", "Volume", volume);
    bpm          = cfg.GetInt("Transport", "Bpm",    bpm);
    metro        = cfg.GetInt("Transport", "Metro",  metro);
    insert       = cfg.GetInt("Transport", "Insert", insert);
    view         = cfg.GetInt("UI", "View", view);
    if(windowWidth  < 640)  windowWidth  = 640;
    if(windowHeight < 360)  windowHeight = 360;
    if(wave < 0 || wave > 3) wave = 0;
    if(volume < 5 || volume > 100) volume = 55;
    if(bpm < 30 || bpm > 240) bpm = 120;
    if(metro < 0 || metro > 1) metro = 1;
    if(insert < 0 || insert > 1) insert = 0;
    if(view < 0 || view > 1) view = 0;

    // Инструмент: волна + огибающая. Огибающая — одной строкой «t v t v ...»
    // (t — с в окне 0..2, v — 0..1), до 32 точек.
    {
        const s32 w = cfg.GetInt("Instrument", "Wave", (s32)instrument.wave);
        if(w >= 0 && w < 4) instrument.wave = (u32)w;
        const char* env = cfg.GetString("Instrument", "Env", "");
        if(env && env[0]) {
            s32 n = 0;
            const char* p = env;
            while(n < 32) {
                double td = 0, vd = 0;
                int adv = 0;
                const int got = std::sscanf(p, "%lf %lf %n", &td, &vd, &adv);
                if(got < 2 || adv <= 0) break;
                f32 t = (f32)td, v = (f32)vd;
                if(t < 0) t = 0;
                else if(t > (f32)Smp::SynthEngine::kEnvWindow) t = (f32)Smp::SynthEngine::kEnvWindow;
                if(v < 0) v = 0;
                else if(v > 1) v = 1;
                instrument.envT[n] = t;
                instrument.envV[n] = v;
                ++n;
                p += adv;
            }
            if(n > 0) instrument.envN = n;
        }
    }
}

void SmpSettings::Save() const {
    ConfigFile cfg;
    cfg.SetInt("Window", "Width",  windowWidth);
    cfg.SetInt("Window", "Height", windowHeight);
    cfg.SetInt("Sound", "Wave",   wave);
    cfg.SetInt("Sound", "Volume", volume);
    cfg.SetInt("Transport", "Bpm",    bpm);
    cfg.SetInt("Transport", "Metro",  metro);
    cfg.SetInt("Transport", "Insert", insert);
    cfg.SetInt("UI", "View", view);
    cfg.SetInt("Instrument", "Wave", (s32)instrument.wave);
    // Огибающая одной строкой «t v t v ...» — до 32 точек (~400 символов).
    {
        char buf[512];
        s32 off = 0;
        const s32 n = (instrument.envN < 1) ? 0 : ((instrument.envN > 32) ? 32 : instrument.envN);
        for(s32 i = 0; i < n; ++i)
            off += std::snprintf(buf + off, (size_t)(sizeof buf - 1 - off),
                                 "%s%.3f %.4f", i ? " " : "",
                                 (double)instrument.envT[i], (double)instrument.envV[i]);
        cfg.SetString("Instrument", "Env", buf);
    }
    cfg.Save(m_paths.SettingsFile().c_str());
}

const char* SmpSettings::RecordingsDir() const {
    // Кэшировать бессмысленно: строка короткая, вызов — единица за запуск.
    static char dir[512];
    String tmp;
    m_paths.BuildConfigDirectoryPath(tmp, "recordings");
    std::snprintf(dir, sizeof dir, "%s", tmp.c_str());
    return dir;
}
