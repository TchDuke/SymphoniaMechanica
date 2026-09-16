// SymphoniaMechanica — музыкальный синтезатор (GUI на фреймворке HID/SDL2).
//
// Окно: строка меню (Сигнал/Громкость/Вид/Транспорт/Метроном/Запись/Файл),
// два вида над одной лентой времени (36 дорожек × 64 доли) — «16 каналов»
// (полосы + клавиатура C2..B4 ниже) и «Клавиши» (вертикальная клавиатура
// слева, НЧ внизу; клетка кликом, штрих растяжкой — для людей без навыков
// игры), строка состояния. Звук идёт отдельным потоком через общий микшер
// Libs/Audio — окно можно сворачивать и закрывать, тракт живёт до выхода.
//
// Живой канал (AppAgent): --agent-serve поднимает сокет, --agent "<команда>"
// — разовый клиент. Свои команды: note/notef/wave/vol/rec/play/stop/
// ins/notes/clear/metro/bpm/insmode/view/cell/cellc/paint/preview/env/
// astats/state.

#include "Core/Types.hpp"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HID_renderer.hpp"
#include "HID_system.hpp"
#include "HID_sdl.hpp"
#include "AppFramework/AppAgent.hpp"
#include "Fonts/HID_font.hpp"

#include "Gport/GportSdl.hpp"

#include "Synth/SynthEngine.hpp"
#include "Synth/AudioBus.hpp"
#include "SmpScreen.hpp"
#include "SmpSettings.hpp"

static Smp::SmpScreen* s_screen = nullptr;

// Строка состояния для живого канала — ровно та, что человек видит внизу окна.
static const char* agentStatus() {
    return s_screen ? s_screen->StatusText() : "";
}

// Нумерация/имена волн — как в движке: 0 пила, 1 квадрат, 2 треугольник, 3 синус.
static s32 waveFromName(const char* t) {
    if(!std::strcmp(t, "0") || !std::strcmp(t, "saw"))       return 0;
    if(!std::strcmp(t, "1") || !std::strcmp(t, "square")
       || !std::strcmp(t, "sq"))                             return 1;
    if(!std::strcmp(t, "2") || !std::strcmp(t, "triangle")
       || !std::strcmp(t, "tri"))                            return 2;
    if(!std::strcmp(t, "3") || !std::strcmp(t, "sin"))       return 3;
    return -1;
}

// СОБСТВЕННЫЕ КОММАНДЫ живого канала (AppAgent::SetCommandHook): строка,
// которую общий канал не понял, отдаётся сюда.
static bool agentHook(const char* line, char* reply, s32 replySize) {
    char a[32], b[32], c[32], d[32];
    a[0] = b[0] = c[0] = d[0] = 0;
    std::sscanf(line, "%31s %31s %31s %31s", a, b, c, d);
    if(!a[0]) return false;

    if(!std::strcmp(a, "note")) {
        const s32 n = std::atoi(b);
        if(n < 0 || n > 127) { std::snprintf(reply, (size_t)replySize, "миди 0..127"); return true; }
        s_screen->AgentNoteOn(n);
        return true;
    }
    if(!std::strcmp(a, "notef")) {
        const s32 n = std::atoi(b);
        if(n >= 0 && n <= 127) s_screen->AgentNoteOff(n);
        return true;
    }
    if(!std::strcmp(a, "wave")) {
        const s32 w = waveFromName(b);
        if(w < 0) { std::snprintf(reply, (size_t)replySize, "saw|square|tri|sin"); return true; }
        s_screen->AgentWave(w);
        return true;
    }
    if(!std::strcmp(a, "vol")) {
        const s32 v = std::atoi(b);
        if(v < 5 || v > 100) { std::snprintf(reply, (size_t)replySize, "5..100"); return true; }
        s_screen->AgentVolume(v);
        return true;
    }
    if(!std::strcmp(a, "rec")) {
        if(std::strcmp(b, "start") && std::strcmp(b, "stop")) {
            std::snprintf(reply, (size_t)replySize, "start|stop"); return true;
        }
        s_screen->AgentRecord(std::strcmp(b, "start") == 0);
        return true;
    }
    if(!std::strcmp(a, "play")) {
        s_screen->AgentPlay();
        return true;
    }
    if(!std::strcmp(a, "stop")) {
        s_screen->AgentStop();
        return true;
    }
    if(!std::strcmp(a, "ins")) {
        // ins <дорожка 0..35> <доля 0..63> <миди 0..127>
        const s32 r = std::atoi(b), bb = std::atoi(c), n = std::atoi(d);
        if(r < 0 || r > 35 || bb < 0 || bb > 63 || n < 0 || n > 127) {
            std::snprintf(reply, (size_t)replySize, "дорожка 0..35 доля 0..63 миди 0..127");
            return true;
        }
        s_screen->AgentInsert(r, bb, n);
        return true;
    }
    if(!std::strcmp(a, "notes")) {
        s_screen->AgentNotes(reply, replySize);
        return true;
    }
    if(!std::strcmp(a, "clear")) {
        s_screen->AgentClear();
        return true;
    }
    if(!std::strcmp(a, "metro")) {
        if(std::strcmp(b, "on") && std::strcmp(b, "off")) {
            std::snprintf(reply, (size_t)replySize, "on|off"); return true;
        }
        s_screen->AgentMetro(std::strcmp(b, "on") == 0);
        return true;
    }
    if(!std::strcmp(a, "bpm")) {
        const s32 v = std::atoi(b);
        if(v < 30 || v > 240) { std::snprintf(reply, (size_t)replySize, "30..240"); return true; }
        s_screen->AgentBpm(v);
        return true;
    }
    if(!std::strcmp(a, "insmode")) {
        if(std::strcmp(b, "on") && std::strcmp(b, "off")) {
            std::snprintf(reply, (size_t)replySize, "on|off"); return true;
        }
        s_screen->AgentInsertMode(std::strcmp(b, "on") == 0);
        return true;
    }
    if(!std::strcmp(a, "view")) {
        if(!std::strcmp(b, "tracks") || !std::strcmp(b, "channels")) {
            s_screen->AgentView(false);
            return true;
        }
        if(!std::strcmp(b, "roll") || !std::strcmp(b, "keys")) {
            s_screen->AgentView(true);
            return true;
        }
        std::snprintf(reply, (size_t)replySize, "tracks|roll");
        return true;
    }
    if(!std::strcmp(a, "cell")) {
        // cell <строка 0..35> <доля 0..63> — переключить клетку вида «Клавиши»
        const s32 r = std::atoi(b), bb = std::atoi(c);
        if(r < 0 || r > 35 || bb < 0 || bb > 63) {
            std::snprintf(reply, (size_t)replySize, "строка 0..35 доля 0..63");
            return true;
        }
        s_screen->AgentCell(r, bb);
        return true;
    }
    if(!std::strcmp(a, "cellc")) {
        // cellc <строка 0..35> <доля 0..63> — Ctrl-клетка (стаккато)
        const s32 r = std::atoi(b), bb = std::atoi(c);
        if(r < 0 || r > 35 || bb < 0 || bb > 63) {
            std::snprintf(reply, (size_t)replySize, "строка 0..35 доля 0..63");
            return true;
        }
        s_screen->AgentCellCtrl(r, bb);
        return true;
    }
    if(!std::strcmp(a, "paint")) {
        // paint <строка 0..35> <доля1> <доля2> — штрих: все клетки lo..hi
        const s32 r = std::atoi(b), b1 = std::atoi(c), b2 = std::atoi(d);
        if(r < 0 || r > 35 || b1 < 0 || b1 > 63 || b2 < 0 || b2 > 63) {
            std::snprintf(reply, (size_t)replySize, "строка 0..35 доля 0..63 (×2)");
            return true;
        }
        s_screen->AgentPaint(r, b1, b2);
        return true;
    }
    if(!std::strcmp(a, "preview")) {
        // preview [миди 0..127] — прогон огибающей (по умолчанию 60 = До4)
        s32 n = 60;
        if(b[0]) n = std::atoi(b);
        if(n < 0 || n > 127) {
            std::snprintf(reply, (size_t)replySize, "миди 0..127");
            return true;
        }
        s_screen->AgentPreview(n);
        return true;
    }
    if(!std::strcmp(a, "env")) {
        s_screen->AgentEnv(reply, replySize);
        return true;
    }
    if(!std::strcmp(a, "astats")) {
        // Сводка звуковой линии: срывы/голодовки кольца (для приёмки щелчков).
        s_screen->AgentLineStats(reply, replySize);
        return true;
    }
    if(!std::strcmp(a, "state")) {
        std::snprintf(reply, (size_t)replySize, "%s", s_screen->StatusText());
        return true;
    }
    return false;   // не наша — пусть канал ответит, что не понял
}

// Шрифт: рядом с исполняемым файлом, затем из исходников.
static bool loadFont(HID_font& font) {
    char path[1024];
    char* base = SDL_GetBasePath();
    if(base) {
        std::snprintf(path, sizeof path, "%sdefault.vfont", base);
        SDL_free(base);
        if(font.LoadFromFile(path)) return true;
    }
    if(font.LoadFromFile("assets/default.vfont")) return true;
    if(font.LoadFromFile("../Libs/Graph/Fonts/default.vfont")) return true;
    return false;
}

int main(s32 argc, char** argv) {
    // Клиент живого канала: послать команду УЖЕ ЗАПУЩЕННОМУ приложению.
    { const s32 rc = AppAgent::ClientMain(argc, argv, "smp"); if (rc >= 0) { return rc; } }

    SmpSettings settings;
    settings.Init(argv && argv[0] ? argv[0] : "smp");
    settings.Load();

    if(SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    // Платформа: опрос клавиш и перевод событий системы.
    static HID_sdl platform;
    HID_set_platform(&platform);
    HID_sdl::Init();

    s32 winW = settings.windowWidth, winH = settings.windowHeight;
    SDL_Window* window = SDL_CreateWindow(
        "SymphoniaMechanica — Синтезатор",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        winW, winH, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if(!window) { std::fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    Grf::GportSdl videoOut;
    if(!videoOut.InitForWindow(window)) {
        std::fprintf(stderr, "GportSdl: %s\n", SDL_GetError());
        SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }

    HID_renderer renderer;
    renderer.Attach(&videoOut);

    HID_font font;
    if(!loadFont(font))
        std::fprintf(stderr, "[font] default.vfont не найден — текст не будет виден\n");

    // Звук — до окна: он живёт своим потоком и не зависит от интерфейса.
    Smp::SynthEngine engine;
    engine.Init(48000);
    Smp::AudioBus bus(&engine);
    if(!bus.Start())
        std::fprintf(stderr, "[smp] звукового устройства нет — интерфейс будет молчать\n");

    Smp::SmpScreen screen(&bus);
    s_screen = &screen;
    SDL_GetWindowSize(window, &winW, &winH);
    screen.Resize(winW, winH);
    if(settings.RecordingsReady()) screen.SetRecordDir(settings.RecordingsDir());
    screen.ApplySound(settings.wave, settings.volume);
    screen.ApplyTransport(settings.bpm, settings.metro != 0, settings.insert != 0);
    screen.ApplyInstrument(settings.instrument.wave, settings.instrument.envT,
                           settings.instrument.envV, settings.instrument.envN);
    screen.SetView(settings.view);

    HID_system hid;
    hid.SetRoot(&screen);

    // Живой канал (--agent-serve): команды по одной, ответ по готовому кадру.
    if(AppAgent::BeginIfRequested(argc, argv, "smp")) {
        AppAgent::SetMenuBar(&screen.MenuBar());
        AppAgent::SetStatusProvider(agentStatus);
        AppAgent::SetCommandHook(agentHook);
    }
    hid.SetDrawContext(&renderer, &font, 1.0f, 500, &settings);

    bool running = true;
    while(running) {
        SDL_Event ev;
        while(SDL_PollEvent(&ev)) {
            if(ev.type == SDL_QUIT) { running = false; continue; }

            if(ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                winW = ev.window.data1; winH = ev.window.data2;
                videoOut.Resize((u16)winW, (u16)winH);
                renderer.Attach(&videoOut);
                screen.Resize(winW, winH);
                continue;
            }

            // F10 открывает/закрывает меню (всегда; функц. клавиша мимо IME).
            if(ev.type == SDL_KEYDOWN && ev.key.repeat == 0 && ev.key.keysym.sym == SDLK_F10) {
                HID_event mf{};
                mf.type = HID_event_type::MenuFocus;
                hid.ProcessEvent(mf);
                continue;
            }

            HID_event h;
            if(!HID_sdl::Translate(&ev, h)) h.type = HID_event_type::None;
            if(h.type != HID_event_type::None)
                hid.ProcessEvent(h);
        }

        if(screen.WantsQuit()) running = false;

        renderer.BeginFrame(24, 24, 30);
        hid.Draw();
        if(AppAgent::Active()) {
            AppAgent::Serve(&renderer, winW, winH);
            if(AppAgent::WantsQuit()) running = false;
        }
        renderer.EndFrame();
    }

    // Сохранить состояние на следующее включение.
    settings.windowWidth  = winW;
    settings.windowHeight = winH;
    settings.instrument   = screen.CurrentInstrument();
    settings.wave         = screen.Wave();
    settings.volume       = screen.VolumePct();
    settings.bpm          = screen.Bpm();
    settings.metro        = screen.Metro() ? 1 : 0;
    settings.insert       = screen.InsertMode() ? 1 : 0;
    settings.view         = screen.ViewRoll() ? 1 : 0;
    settings.Save();

    bus.Stop();          // сначала трек: в нем ещё играет хвост последнего звука
    AppAgent::End();
    videoOut.Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
