#include "ModelRender.hpp"
#include "ModelCorpus.hpp"
#include <algorithm>

using namespace iprop::sfml_host;
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    checkModelCorpus(argv[1], [](const ModelTexture& t) {
        sf::Texture texture;
        uploadModelTexture(texture, t, true);
        auto image = texture.copyToImage();
        if (!std::equal(t.data->rgba.begin(), t.data->rgba.end(), image.getPixelsPtr())) throw std::runtime_error("Texture readback mismatch");
        for (const auto& a : t.data->images) {
            if (!texture.loadFromImage(sf::Image({a.info.width, a.info.height}, a.pixels.data()))) throw std::runtime_error("Map upload failed");
            auto map = texture.copyToImage();
            if (!std::equal(a.pixels.begin(), a.pixels.end(), map.getPixelsPtr())) throw std::runtime_error("Map readback mismatch");
        }
    });
}
