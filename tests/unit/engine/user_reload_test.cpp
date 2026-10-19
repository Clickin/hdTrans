// The automatic reload of the user dictionary file.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include "core/engine.h"
#include "core/engine/user_watch.h"
#include "hdtest.h"

using namespace hdTrans;
using namespace std::chrono_literals;

namespace {

std::string Fixture() {
    return (std::filesystem::path(__FILE__).parent_path() / "testdata" / "session").string();
}

struct TempFolder {
    std::filesystem::path path;
    TempFolder() {
        std::error_code ec;
        path = std::filesystem::temp_directory_path(ec) /
               ("hdtrans-userdict-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(path, ec);
    }
    ~TempFolder() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

bool Write(const std::filesystem::path& path, std::string_view text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
    return f.good();
}

}  // namespace

HD_TEST(UserFileWatchDue) {
    TempFolder temp;
    const std::string path = (temp.path / "userdict.tsv").string();
    internal::FileWatch w;
    internal::FileVersion found;
    const auto t0 = std::chrono::steady_clock::now();
    // A missing file is the version of no file.
    HD_CHECK(!w.Due(path, t0, 1s, found));
    HD_REQUIRE(Write(path, "猫\t고양이\n"));
    // At most one check per interval.
    HD_CHECK(!w.Due(path, t0 + 500ms, 1s, found));
    HD_CHECK(w.Due(path, t0 + 1s, 1s, found));
    HD_CHECK(found.present);
    HD_CHECK_EQ(found.stamp.size, uint64_t{14});
    // A version that failed is not due again until the file changes.
    w.failed = found;
    w.hasFailed = true;
    HD_CHECK(!w.Due(path, t0 + 2s, 1s, found));
    HD_REQUIRE(Write(path, "猫\t냥이\n"));
    HD_CHECK(w.Due(path, t0 + 3s, 1s, found));
    w.loaded = found;
    w.hasFailed = false;
    HD_CHECK(!w.Due(path, t0 + 4s, 1s, found));
    std::error_code ec;
    std::filesystem::remove(path, ec);
    HD_CHECK(w.Due(path, t0 + 5s, 1s, found));
    HD_CHECK(!found.present);
}

HD_TEST(UserFileReload) {
    TempFolder temp;
    const auto user = temp.path / "userdict.tsv";
    Engine engine(false, CacheWrite::Off, UserReload::Auto);
    HD_REQUIRE(engine.Open({Fixture()}, false, user.string()));
    Session session(engine);
    const std::string before = session.Translate("猫");
    HD_CHECK(before.find("고양이") == std::string::npos);
    HD_CHECK(!internal::CheckUserFile(engine));

    // A new file is loaded and taken by the session.
    HD_REQUIRE(Write(user, "# comment\n猫\t고양이\n"));
    const auto old = engine.Dictionaries();
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() != old);
    HD_CHECK(session.Translate("猫").find("고양이") != std::string::npos);

    // A file that fails to load keeps the dictionaries, and is not retried until it changes.
    HD_REQUIRE(Write(user, "猫\n"));
    const auto good = engine.Dictionaries();
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() == good);
    const auto errors = internal::UserFileErrors(engine);
    HD_REQUIRE(errors.size() == 1);
    HD_CHECK(errors[0].kind == dict::LoadErrorKind::Plain);
    HD_CHECK_EQ(errors[0].file, user.string());
    HD_CHECK(!internal::CheckUserFile(engine));
    HD_CHECK(session.Translate("猫").find("고양이") != std::string::npos);
    HD_REQUIRE(Write(user, "猫\t냥이\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(internal::UserFileErrors(engine).empty());
    HD_CHECK(session.Translate("猫").find("냥이") != std::string::npos);

    // A translation starts the check once the interval has passed; the new dictionaries
    // replace the old ones when loaded.
    HD_REQUIRE(Write(user, "猫\t야옹이\n"));
    const auto loaded = engine.Dictionaries();
    std::this_thread::sleep_for(1100ms);
    session.Translate("猫");
    for (int i = 0; i < 500 && engine.Dictionaries() == loaded; i++) std::this_thread::sleep_for(10ms);
    HD_CHECK(engine.Dictionaries() != loaded);
    HD_CHECK(session.Translate("猫").find("야옹이") != std::string::npos);

    // Removing the file removes the user dictionary.
    std::error_code ec;
    std::filesystem::remove(user, ec);
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK_EQ(session.Translate("猫"), before);

    // An explicit Reload keeps working.
    HD_REQUIRE(Write(user, "猫\t고양이\n"));
    HD_CHECK(engine.Reload());
    HD_CHECK(!internal::CheckUserFile(engine));
    HD_CHECK(session.Translate("猫").find("고양이") != std::string::npos);

    // Without UserReload::Auto nothing is checked.
    Engine manual(false, CacheWrite::Off);
    HD_REQUIRE(manual.Open({Fixture()}, false, user.string()));
    HD_REQUIRE(Write(user, "猫\t냥이\n"));
    HD_CHECK(!internal::CheckUserFile(manual));
}
