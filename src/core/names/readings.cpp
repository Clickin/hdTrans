#include "core/names/readings.h"

#include <string>
#include <string_view>
#include <utility>

#include "core/names/chars.h"
#include "core/names/registry.h"

namespace hdTrans::names {

namespace {

// Readings of common names whose dictionary person reading is missing or not the usual one.
const std::pair<std::string_view, std::string_view> kBuiltinReadings[] = {
    // Given names that are also common words.
    {"光", "ひかる"}, {"桜", "さくら"}, {"翼", "つばさ"}, {"葵", "あおい"}, {"愛", "あい"}, {"空", "そら"},
    {"楓", "かえで"}, {"蛍", "ほたる"}, {"薫", "かおる"}, {"雫", "しずく"}, {"恋", "れん"}, {"希", "のぞみ"},
    {"凛", "りん"}, {"茜", "あかね"}, {"樹", "いつき"}, {"翔", "しょう"}, {"蒼", "あお"}, {"渚", "なぎさ"},
    {"遥", "はるか"}, {"悠", "ゆう"}, {"誠", "まこと"}, {"健", "けん"}, {"優", "ゆう"}, {"舞", "まい"},
    {"彩", "あや"}, {"唯", "ゆい"}, {"真", "まこと"}, {"瞳", "ひとみ"}, {"静", "しずか"}, {"円", "まどか"},
    {"椿", "つばき"}, {"菫", "すみれ"}, {"雪", "ゆき"}, {"霞", "かすみ"}, {"司", "つかさ"}, {"忍", "しのぶ"},
    {"春香", "はるか"}, {"翔太", "쇼타"}, {"陽菜", "ひな"}, {"結衣", "ゆい"}, {"美咲", "みさき"},
    {"大輔", "だいすけ"}, {"健太", "けんた"}, {"拓也", "たくや"}, {"直樹", "なおき"}, {"和也", "かずや"},
    {"太郎", "たろう"}, {"花子", "はなこ"}, {"明日菜", "あすな"}, {"千早", "ちはや"},
    // Surnames that are also common words or have several readings.
    {"森", "もり"}, {"林", "はやし"}, {"山田", "やまだ"}, {"田中", "たなか"}, {"佐藤", "さとう"}, {"鈴木", "すずき"},
    {"高橋", "たかはし"}, {"渡辺", "わたなべ"}, {"伊藤", "いとう"}, {"中村", "なかむら"}, {"小林", "こばやし"},
    {"加藤", "かとう"}, {"吉田", "よしだ"}, {"山本", "やまもと"}, {"佐々木", "ささき"}, {"山口", "やまぐち"},
    {"松本", "まつもと"}, {"井上", "いのうえ"}, {"木村", "きむら"}, {"清水", "しみず"}, {"斎藤", "さいとう"},
    {"藤井", "ふじい"}, {"池田", "いけだ"}, {"橋本", "はしもと"}, {"石川", "いしかわ"}, {"前田", "まえだ"},
    {"小川", "おがわ"}, {"岡田", "おかだ"}, {"長谷川", "はせがわ"}, {"村上", "むらかみ"}, {"近藤", "こんどう"},
    {"坂本", "さかもと"}, {"遠藤", "えんどう"}, {"青木", "あおき"}, {"藤田", "ふじた"}, {"西村", "にしむら"},
    {"福田", "ふくだ"}, {"太田", "おおた"}, {"三浦", "みうら"}, {"藤原", "ふじわら"}, {"岡本", "おかもと"},
    {"松田", "まつだ"}, {"中島", "なかじま"}, {"原田", "はらだ"}, {"小野", "おの"}, {"竹内", "たけうち"},
    {"金子", "かねこ"}, {"和田", "わだ"}, {"中野", "なかの"}, {"上田", "うえだ"}, {"原", "はら"}, {"泊", "とまり"},
    {"北条", "ほうじょう"}, {"上条", "かみじょう"}, {"東条", "とうじょう"}, {"西園寺", "さいおんじ"},
};

}  // namespace

namespace internal {

void AddBuiltinReadings(std::map<std::string, std::string>& readings) {
    for (const auto& [jp, kana] : kBuiltinReadings) readings[std::string(jp)] = std::string(kana);
}

}  // namespace internal

bool Registry::LoadReadings(std::string_view text) {
    scoresValid_ = false;
    if (tables_.use_count() != 1) tables_ = std::make_shared<Tables>(*tables_);
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    bool ok = true;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        line = internal::TrimCR(line);
        const std::string_view trimmed = internal::TrimSpace(line);
        if (trimmed.empty() || trimmed.starts_with("//")) continue;
        const std::size_t tab = line.find('\t');
        if (tab == std::string_view::npos) {
            ok = false;
            continue;
        }
        std::string_view jp = internal::TrimSpace(line.substr(0, tab));
        std::string_view value = line.substr(tab + 1);
        value = value.substr(0, value.find('\t'));
        value = internal::TrimSpace(value);
        if (jp.empty() || value.empty() || (!internal::IsKanaText(value) && !internal::IsHangulText(value))) {
            ok = false;
            continue;
        }
        tables_->readings[std::string(jp)] = std::string(value);
    }
    return ok;
}

}  // namespace hdTrans::names
