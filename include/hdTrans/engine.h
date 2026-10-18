#pragma once
// C++ API of the engine. All strings are UTF-8.
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans {

class Engine {
public:
    // Loads the folders in order; later folders override earlier ones. Returns false when
    // no dictionary could be loaded.
    bool Open(const std::vector<std::string>& dirs);

    // Reloads the folders given to Open.
    bool Reload();

    // Translates one or more "\n"-separated lines ("\r" is kept). Names found anywhere
    // in the text get one rendering in every line.
    std::string Translate(std::string_view text) const;

    bool loaded() const { return loaded_; }

private:
    std::vector<std::string> dirs_;
    bool loaded_ = false;
};

}  // namespace hdTrans
