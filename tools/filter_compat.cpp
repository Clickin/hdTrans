#include <algorithm>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/regex.hpp>
#include <boost/version.hpp>

#include "core/filter/ehnd.h"
#include "core/filter/filter.h"
#include "pcre2.h"

namespace {

using namespace hdTrans::filter;

struct Options {
    std::string ehnd, pre, post, report, locale = "en_US.UTF-8";
    std::size_t maximum = 500;
};

void Usage() {
    std::cout << "filter_compat --ehnd DIR --pre JP_DIR --post KO_DIR --report DIR\n"
                 "              [--max-lines 500] [--locale en_US.UTF-8]\n"
                 "Use --max-lines 0 for every nonempty line. Reports may contain input texts.\n"
                 "Build separately with Boost.Regex, the core and PCRE2 libraries,\n"
                 "-fno-access-control and -DPCRE2_CODE_UNIT_WIDTH=16.\n"
                 "The reference maps each UTF-16 unit to one wchar_t. It does not emulate\n"
                 "Windows character traits or an earlier Boost version. Post starts with default toggles.\n";
}

bool Parse(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) return false;
        const std::string key = argv[i], value = argv[i + 1];
        if (key == "--ehnd") options.ehnd = value;
        else if (key == "--pre") options.pre = value;
        else if (key == "--post") options.post = value;
        else if (key == "--report") options.report = value;
        else if (key == "--locale") options.locale = value;
        else if (key == "--max-lines") {
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) return false;
            options.maximum = std::stoull(value);
        } else return false;
    }
    return !options.ehnd.empty() && !options.pre.empty() && !options.post.empty() && !options.report.empty();
}

std::wstring Wide(std::u16string_view text) { return {text.begin(), text.end()}; }
std::u16string Units(std::wstring_view text) { return {text.begin(), text.end()}; }

struct Pattern {
    std::shared_ptr<boost::wregex> regex;
    std::string error;
};

struct Event {
    std::size_t rule;
    std::u16string after;
    bool operator==(const Event&) const = default;
};

struct Stats {
    std::size_t evaluated = 0, matched = 0, changed = 0, runtimeErrors = 0;
    std::set<std::size_t> matchedRules;
};

struct Result {
    std::string text;
    std::vector<Event> events;
};

std::vector<std::u16string_view> Fields(std::u16string_view line) {
    std::vector<std::u16string_view> fields;
    for (std::size_t i = 0, previous = 0; i <= line.size(); i++) {
        const bool comment = i < line.size() && i > 0 && line[i] == u'/' && line[i - 1] == u'/';
        if (i == line.size() || line[i] == u'\t' || line[i] == u'\n' || comment) {
            fields.push_back(line.substr(previous, i - previous));
            previous = i + 1;
        }
    }
    return fields;
}

std::unordered_map<int32_t, std::u16string> Conditions(const Filters& filters, const std::vector<Source>& sources,
                                                    const std::vector<Error>& errors) {
    struct Condition {
        Stage stage;
        int layer;
        std::u16string pattern;
    };
    std::vector<Condition> raw;
    for (const Source& source : sources) {
        if (source.kind != Source::Kind::SkipLayer) continue;
        const std::u16string text = Utf16(source.text);
        int number = 0;
        for (std::size_t pos = 0; pos < text.size();) {
            const std::size_t nl = text.find(u'\n', pos);
            const std::size_t end = nl == text.npos ? text.size() : nl + 1;
            const std::u16string_view line(text.data() + pos, end - pos);
            pos = end;
            number++;
            if (line.starts_with(u"//")) continue;
            const auto fields = Fields(line);
            if (fields.size() < 2 || !(fields[0].starts_with(u"PRE") || fields[0].starts_with(u"POST"))) continue;
            if (std::any_of(errors.begin(), errors.end(), [&](const Error& error) {
                    return error.file == source.name && error.line == number;
                })) continue;
            const long long layer = std::strtoll(Utf8(fields[1]).c_str(), nullptr, 10);
            raw.push_back({fields[0].starts_with(u"PRE") ? Stage::Pre : Stage::Post,
                           static_cast<int>(std::clamp<long long>(layer, INT_MIN, INT_MAX)),
                           fields.size() > 2 ? std::u16string(fields[2]) : std::u16string()});
        }
    }
    std::stable_sort(raw.begin(), raw.end(), [](const Condition& a, const Condition& b) { return a.layer < b.layer; });
    std::unordered_map<int32_t, std::u16string> out;
    for (const Stage stage : {Stage::Pre, Stage::Post}) {
        const auto& program = stage == Stage::Pre ? filters.pre_ : filters.post_;
        std::size_t index = 0;
        for (const Condition& condition : raw) {
            if (condition.stage != stage) continue;
            if (index >= program.conditions.size() || condition.layer != program.conditions[index].layer)
                throw std::runtime_error("condition mapping failed");
            out.emplace(program.conditions[index++].regex, condition.pattern);
        }
        if (index != program.conditions.size()) throw std::runtime_error("condition mapping failed");
    }
    return out;
}

bool ReplacePcre(const pcre2_code_16* regex, std::u16string& text, std::u16string_view dest, pcre2_match_data_16* data,
                 std::size_t& runtimeErrors) {
    const auto* subject = reinterpret_cast<PCRE2_SPTR16>(text.data());
    const PCRE2_SIZE* offsets = pcre2_get_ovector_pointer_16(data);
    const uint32_t pairs = pcre2_get_ovector_count_16(data);
    std::u16string out;
    std::size_t copied = 0, previous = 0, start = 0;
    uint32_t options = 0;
    bool matched = false;
    while (start <= text.size()) {
        const int rc = pcre2_match_16(regex, subject, text.size(), start, options, data, nullptr);
        if (rc == PCRE2_ERROR_NOMATCH && options != 0) {
            options = 0;
            start++;
            continue;
        }
        if (rc == PCRE2_ERROR_NOMATCH) break;
        if (rc < 0) {
            runtimeErrors++;
            return false;
        }
        matched = true;
        out.append(text, copied, offsets[0] - copied);
        AppendReplacement(out, dest, text, offsets, std::min<uint32_t>(pairs, static_cast<uint32_t>(rc)), previous);
        copied = previous = offsets[1];
        start = offsets[1];
        options = offsets[0] == offsets[1] ? PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED : 0;
    }
    if (!matched) return false;
    out.append(text, copied);
    if (out == text) return false;
    text = std::move(out);
    return true;
}

class Comparison {
public:
    Comparison(const Filters& filters, const std::vector<Source>& sources, const std::vector<Error>& errors,
               const std::string& locale)
        : filters_(filters), conditions_(Conditions(filters, sources, errors)), locale_(locale.c_str()) {
        for (const auto& [index, pattern] : conditions_) {
            (void)index;
            Compile(pattern);
        }
        for (const auto* program : {&filters_.pre_, &filters_.post_}) {
            for (const auto& rule : program->rules) {
                if (rule.regex >= 0) Compile(rule.src);
            }
        }
    }

    Pattern& Compile(const std::u16string& source) {
        const auto [it, added] = patterns.try_emplace(source);
        if (added) {
            try {
                auto regex = std::make_shared<boost::wregex>();
                regex->imbue(locale_);
                regex->assign(Wide(source));
                it->second.regex = std::move(regex);
            } catch (const boost::regex_error& error) {
                it->second.error = error.what();
            }
        }
        return it->second;
    }

    Result Run(Stage stage, const std::string& input, bool boost, Stats& stats) {
        const auto& program = stage == Stage::Pre ? filters_.pre_ : filters_.post_;
        auto toggles = filters_.Start();
        auto text = Utf16(input);
        std::unique_ptr<pcre2_match_data_16, decltype(&pcre2_match_data_free_16)> data(
            pcre2_match_data_create_16(filters_.pairs_, nullptr), pcre2_match_data_free_16);
        std::vector<uint32_t> seen(program.matcher.Size());
        uint32_t stamp = 0;
        bool dirty = true;
        auto has = [&](int32_t id) {
            if (id < 0) return true;
            if (dirty) {
                program.matcher.Scan(text, seen, ++stamp);
                dirty = false;
            }
            return seen[static_cast<std::size_t>(id)] == stamp;
        };
        auto search = [&](int32_t index) {
            const int rc = pcre2_match_16(filters_.regexes_[static_cast<std::size_t>(index)],
                                          reinterpret_cast<PCRE2_SPTR16>(text.data()), text.size(), 0, 0, data.get(), nullptr);
            if (rc < 0 && rc != PCRE2_ERROR_NOMATCH) stats.runtimeErrors++;
            return rc >= 0;
        };
        Result result;
        int layer = -1;
        bool skipped = false, guard = false;
        for (std::size_t index = 0; index < program.rules.size(); index++) {
            const auto& rule = program.rules[index];
            if (rule.layer != layer) {
                layer = rule.layer;
                skipped = false;
                for (const auto& condition : program.conditions) {
                    if (condition.layer != layer) continue;
                    bool matched = false;
                    if (boost) {
                        auto& pattern = Compile(conditions_.at(condition.regex));
                        if (pattern.regex) {
                            try { matched = boost::regex_search(Wide(text), *pattern.regex); }
                            catch (const std::exception&) { stats.runtimeErrors++; }
                        }
                    } else matched = search(condition.regex);
                    if (!matched) {
                        skipped = true;
                        break;
                    }
                }
            }
            if (skipped || (rule.gate >= 0 && !toggles[static_cast<std::size_t>(rule.gate)])) continue;
            if (rule.kind == Filters::Kind::Guarded) {
                if (!guard) continue;
                if (rule.blockEnd) guard = false;
            }
            bool fired = false;
            if (rule.regex < 0) {
                if (rule.check) fired = has(rule.units[0]);
                else if ((rule.kind == Filters::Kind::Guarded || has(rule.aux)) && has(rule.units[0])) {
                    std::size_t offset = 0;
                    for (std::size_t at; (at = text.find(rule.src, offset)) != text.npos;) {
                        text.replace(at, rule.src.size(), rule.dest);
                        offset = at + rule.dest.size();
                    }
                    fired = offset != 0;
                    dirty = true;
                }
            } else if (has(rule.aux)) {
                stats.evaluated++;
                bool matched = false;
                if (boost) {
                    auto& pattern = Compile(rule.src);
                    if (pattern.regex) {
                        try {
                            const auto before = Wide(text);
                            matched = boost::regex_search(before, *pattern.regex);
                            if (rule.check) fired = matched;
                            else if (matched) {
                                const auto after = boost::regex_replace(before, *pattern.regex, Wide(rule.dest),
                                                                         boost::regex_constants::match_default);
                                fired = after != before;
                                if (fired) text = Units(after);
                            }
                        } catch (const std::exception&) { stats.runtimeErrors++; }
                    }
                } else {
                    matched = search(rule.regex);
                    if (rule.check) fired = matched;
                    else if (matched) fired = ReplacePcre(filters_.regexes_[static_cast<std::size_t>(rule.regex)], text,
                                                          rule.dest, data.get(), stats.runtimeErrors);
                }
                if (matched) {
                    stats.matched++;
                    stats.matchedRules.insert(index);
                }
                if (fired && !rule.check) stats.changed++;
                dirty = dirty || fired;
            }
            if (!fired) continue;
            result.events.push_back({index, text});
            if (rule.sets >= 0) toggles[static_cast<std::size_t>(rule.sets)] = rule.on;
            if (rule.kind == Filters::Kind::Guard) guard = true;
        }
        result.text = Utf8(text);
        return result;
    }

    std::unordered_map<std::u16string, Pattern> patterns;

private:
    const Filters& filters_;
    std::unordered_map<int32_t, std::u16string> conditions_;
    std::locale locale_;
};

struct Input {
    std::string file, text;
    int line = 0;
};

std::vector<Input> Inputs(const std::string& dir, std::size_t maximum) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    std::vector<Input> all;
    for (const auto& path : files) {
        std::ifstream input(path);
        if (!input) throw std::runtime_error("input file cannot be opened");
        std::string line;
        int number = 0;
        while (std::getline(input, line)) {
            number++;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) all.push_back({path.string(), std::move(line), number});
        }
    }
    if (maximum == 0 || all.size() <= maximum) return all;
    std::vector<Input> sample;
    for (std::size_t i = 0; i < maximum; i++) sample.push_back(std::move(all[i * all.size() / maximum]));
    return sample;
}

int Compare(const Options& options) {
    std::filesystem::create_directories(options.report);
    const auto reportPath = std::filesystem::path(options.report) / "details.txt";
    if (std::filesystem::exists(reportPath)) throw std::runtime_error("report already exists");
    std::ofstream report(reportPath);
    if (!report) throw std::runtime_error("report cannot be opened");
    std::vector<Source> sources;
    std::vector<Error> errors;
    if (!ReadEhndFolder(options.ehnd, sources, errors)) throw std::runtime_error("filter folder cannot be read");
    std::erase_if(sources, [](const Source& source) { return source.kind == Source::Kind::UserDict; });
    const auto filters = Build(sources, errors);
    if (!filters) throw std::runtime_error("no filter files");
    Comparison comparison(*filters, sources, errors, options.locale);
    std::size_t pcreRejected = 0, boostRejected = 0, regexRows = 0;
    for (const auto* program : {&filters->pre_, &filters->post_}) {
        for (const auto& rule : program->rules) regexRows += rule.regex >= 0;
    }
    for (const Error& error : errors) {
        pcreRejected += error.message.starts_with("regex rejected");
        report << "LOAD\t" << error.file << ':' << error.line << '\t' << error.message << '\n';
    }
    for (const auto& [pattern, compiled] : comparison.patterns) {
        if (!compiled.error.empty()) {
            boostRejected++;
            report << "BOOST_REJECT\t" << Utf8(pattern) << '\t' << compiled.error << '\n';
        }
    }
    std::cout << "boost=" << BOOST_LIB_VERSION << " locale=" << options.locale << " wchar_bits=" << sizeof(wchar_t) * 8
              << " utf16_unit_mapping=yes post_toggles=defaults\n"
              << "pre_rows=" << filters->Rows(Stage::Pre) << " post_rows=" << filters->Rows(Stage::Post)
              << " regex_rows=" << regexRows << " unique_regex=" << comparison.patterns.size()
              << " pcre_rejected=" << pcreRejected << " boost_unique_rejected=" << boostRejected
              << " load_errors=" << errors.size() << '\n';
    bool failed = !errors.empty() || boostRejected != 0;
    for (const Stage stage : {Stage::Pre, Stage::Post}) {
        const auto inputs = Inputs(stage == Stage::Pre ? options.pre : options.post, options.maximum);
        Stats pcreStats, boostStats;
        std::size_t outputDifferences = 0, selfDifferences = 0, traceDifferences = 0;
        for (const Input& input : inputs) {
            auto toggles = filters->Start();
            std::string actual = input.text;
            filters->Apply(stage, actual, toggles);
            const auto pcre = comparison.Run(stage, input.text, false, pcreStats);
            const auto boost = comparison.Run(stage, input.text, true, boostStats);
            selfDifferences += pcre.text != actual;
            outputDifferences += pcre.text != boost.text;
            traceDifferences += pcre.events != boost.events;
            if (pcre.text != actual || pcre.text != boost.text || pcre.events != boost.events) {
                report << "DIFF\t" << (stage == Stage::Pre ? "pre" : "post") << '\t' << input.file << ':' << input.line
                       << "\nINPUT\t" << input.text << "\nACTUAL\t" << actual << "\nPCRE\t" << pcre.text
                       << "\nBOOST\t" << boost.text << '\n';
                const auto& program = stage == Stage::Pre ? filters->pre_ : filters->post_;
                for (std::size_t i = 0; i < std::max(pcre.events.size(), boost.events.size()); i++) {
                    if (i < pcre.events.size() && i < boost.events.size() && pcre.events[i] == boost.events[i]) continue;
                    for (const auto* events : {&pcre.events, &boost.events}) {
                        if (i >= events->size()) continue;
                        const auto& event = (*events)[i];
                        const auto& rule = program.rules[event.rule];
                        report << (events == &pcre.events ? "PCRE_RULE\t" : "BOOST_RULE\t") << rule.file << ':' << rule.line
                               << '\t' << Utf8(rule.src) << '\t' << Utf8(rule.dest) << "\nAFTER\t" << Utf8(event.after) << '\n';
                    }
                    break;
                }
            }
        }
        std::cout << (stage == Stage::Pre ? "pre" : "post") << " inputs=" << inputs.size()
                  << " output_differences=" << outputDifferences << " pcre_self_differences=" << selfDifferences
                  << " fired_trace_differences=" << traceDifferences << '\n';
        for (const auto* stats : {&pcreStats, &boostStats}) {
            std::cout << "  backend=" << (stats == &pcreStats ? "pcre" : "boost") << " regex_evaluations=" << stats->evaluated
                      << " matched_rules=" << stats->matched << " unique_matched_rules=" << stats->matchedRules.size()
                      << " changed_rules=" << stats->changed << " runtime_errors=" << stats->runtimeErrors << '\n';
        }
        failed = failed || outputDifferences != 0 || selfDifferences != 0 || traceDifferences != 0 ||
                 pcreStats.runtimeErrors != 0 || boostStats.runtimeErrors != 0;
    }
    return failed ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            Usage();
            return 0;
        }
        Options options;
        if (!Parse(argc, argv, options)) {
            Usage();
            return 2;
        }
        return Compare(options);
    } catch (const std::exception& error) {
        std::cerr << "filter_compat: " << error.what() << '\n';
        return 2;
    }
}
