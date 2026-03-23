#include "app/App.h"
#include "core/Config.h"
#include <iostream>
#include <string>

namespace {

int usage() {
    std::cout
        << "usage:\n"
        << "  liquid_glass_engine [config.json]\n"
        << "  liquid_glass_engine --write-template [output.json]\n";
    return 0;
}

}

int main(int argc, char** argv){
    if (argc > 1) {
        const std::string arg = argv[1];
        if (arg == "-h" || arg == "--help") {
            return usage();
        }
        if (arg == "--write-template") {
            lg::ConfigLoader::save_template(argc > 2 ? argv[2] : "configs/generated_template.json");
            return 0;
        }
    }
    lg::App app;
    return app.run(argc > 1 ? argv[1] : "configs/default.json");
}
