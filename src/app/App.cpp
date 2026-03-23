#include "app/App.h"
#include "core/Config.h"
#include "utils/Logger.h"
#include <exception>
namespace lg {
int App::run(const char* config_path){
    init_logger();
    const char* resolved_path = config_path ? config_path : "configs/default.json";
    try {
        auto cfg = ConfigLoader::load(resolved_path);
        log(LogLevel::Info, std::string("loaded config from ") + resolved_path);
        Engine engine;
        if(!engine.initialize(cfg)) {
            log(LogLevel::Error, "engine initialization failed");
            return 1;
        }
        return engine.run();
    } catch (const std::exception& e) {
        log(LogLevel::Error, e.what());
        return 1;
    }
}
}
